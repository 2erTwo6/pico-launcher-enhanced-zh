#include "common.h"
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fat/ff.h"
#include "ThemeFolderRules.h"
#include "ThemeFolderDeleter.h"

#define THEMES_PATH         "/_pico/themes"
#define PATH_BUFFER_SIZE    1024
/// Folders below the theme folder. A theme has files and maybe a bgm folder.
#define MAX_DEPTH           4
#define MAX_ENTRIES         1024

namespace
{
    /// Everything the walk needs, on the heap: the IO thread has a small stack,
    /// and FatFs puts its long name buffer on that stack in every call.
    struct Job
    {
        char path[PATH_BUFFER_SIZE];
        char otherPath[PATH_BUFFER_SIZE];
        /// The theme folder's own path, kept apart to check every delete against.
        char root[PATH_BUFFER_SIZE];
        FILINFO info;
        DIR dirs[MAX_DEPTH + 1];
        DIR parentDir;
        DWORD clusters[MAX_DEPTH + 1];
        u32 pathLengths[MAX_DEPTH + 1];
    };

    /// malloc and free, not new: the launcher has no C++ exceptions, and
    /// new (std::nothrow) pulls the whole exception runtime in (17 KB, and the
    /// launcher no longer booted). malloc just returns null when it can't.
    struct JobDeleter
    {
        void operator()(Job* job) const { free(job); }
    };

    /// Appends "/" and the name; false, and nothing changed, when it would not fit.
    bool AppendSegment(char* path, u32& length, const char* name)
    {
        u32 nameLength = strlen(name);
        if (length + 1 + nameLength + 1 > PATH_BUFFER_SIZE)
        {
            return false;
        }
        path[length] = '/';
        memcpy(path + length + 1, name, nameLength + 1);
        length += 1 + nameLength;
        return true;
    }

    /// "/_pico/themes/<name>", or false when it would not fit.
    bool SetThemePath(char* path, u32& length, const char* folderName)
    {
        length = sizeof(THEMES_PATH) - 1;
        memcpy(path, THEMES_PATH, length + 1);
        return AppendSegment(path, length, folderName);
    }

    /// The start cluster of the folder at this path, or 0 when it isn't one.
    DWORD GetFolderCluster(FILINFO& info, const char* path)
    {
        if (f_stat(path, &info) != FR_OK || !(info.fattrib & AM_DIR))
        {
            return 0;
        }
        return info.fclust;
    }

    /// True when "<path>/.." opens the folder at parentCluster. On a card with a
    /// damaged folder link, a path can open a folder that lives somewhere else
    /// entirely; its ".." entry still names its real parent.
    bool IsChildOf(Job& job, u32 pathLength, DWORD parentCluster)
    {
        memcpy(job.otherPath, job.path, pathLength);
        job.otherPath[pathLength] = 0;
        u32 length = pathLength;
        if (!AppendSegment(job.otherPath, length, ".."))
        {
            return false;
        }
        if (f_opendir(&job.parentDir, job.otherPath) != FR_OK)
        {
            return false;
        }
        bool isChild = job.parentDir.obj.sclust == parentCluster;
        f_closedir(&job.parentDir);
        return isChild;
    }

    /// True when the theme folder with this setting name is the one at `cluster`.
    bool IsSameFolder(Job& job, const char* folderName, DWORD cluster)
    {
        u32 length;
        return SetThemePath(job.otherPath, length, folderName)
            && GetFolderCluster(job.info, job.otherPath) == cluster;
    }

    /// Finds the one entry of /_pico/themes whose name is exactly folderName,
    /// then makes sure the path built from that name opens that very entry. With
    /// code page 437, an accented short name can open another folder through
    /// its 8.3 alias, and f_stat reports back the name it was asked for, so only
    /// the entry's location on the card proves which folder the path means.
    ThemeDeleteResult FindThemeFolder(Job& job, const char* folderName, DWORD& cluster)
    {
        DIR& themes = job.dirs[0];
        if (f_opendir(&themes, THEMES_PATH) != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }

        u32 matches = 0;
        DWORD sector = 0, offset = 0;
        BYTE attributes = 0;
        FRESULT result;
        while ((result = f_readdir(&themes, &job.info)) == FR_OK && job.info.fname[0] != 0)
        {
            if (strcmp(job.info.fname, folderName) == 0)
            {
                matches++;
                sector = job.info.fdirsect;
                offset = job.info.fdiroffs;
                cluster = job.info.fclust;
                attributes = job.info.fattrib;
            }
        }
        f_closedir(&themes);
        if (result != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }
        if (matches != 1 || !(attributes & AM_DIR) || cluster == 0)
        {
            return ThemeDeleteResult::NotFound;
        }
        if (attributes & AM_RDO)
        {
            return ThemeDeleteResult::ReadOnly;
        }

        // No other entry of /_pico/themes may point at the same folder: that is
        // a damaged card, and deleting would take the other theme too.
        if (f_opendir(&themes, THEMES_PATH) != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }
        u32 sameCluster = 0;
        while ((result = f_readdir(&themes, &job.info)) == FR_OK && job.info.fname[0] != 0)
        {
            if (job.info.fclust == cluster)
            {
                sameCluster++;
            }
        }
        f_closedir(&themes);
        if (result != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }
        if (sameCluster != 1)
        {
            return ThemeDeleteResult::ReadError;
        }

        u32 length;
        if (!SetThemePath(job.path, length, folderName))
        {
            return ThemeDeleteResult::PathTooLong;
        }
        if (f_stat(job.path, &job.info) != FR_OK
            || job.info.fdirsect != sector || job.info.fdiroffs != offset || job.info.fclust != cluster)
        {
            return ThemeDeleteResult::NotFound;
        }
        return ThemeDeleteResult::Ok;
    }

    /// Walks the folder without recursion, one DIR per level. Refuses on the
    /// first thing a delete could get wrong or could not finish.
    ThemeDeleteResult WalkThemeFolder(Job& job, const char* folderName, DWORD rootCluster, DWORD themesCluster,
        DWORD picoCluster, ThemeDeleteCounts& counts)
    {
        u32 length;
        if (!SetThemePath(job.path, length, folderName))
        {
            return ThemeDeleteResult::PathTooLong;
        }
        if (f_opendir(&job.dirs[0], job.path) != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }
        if (job.dirs[0].obj.sclust != rootCluster)
        {
            f_closedir(&job.dirs[0]);
            return ThemeDeleteResult::NotFound;
        }
        if (!IsChildOf(job, length, themesCluster))
        {
            f_closedir(&job.dirs[0]);
            return ThemeDeleteResult::ReadError;
        }
        job.clusters[0] = rootCluster;
        job.pathLengths[0] = length;

        int depth = 0;
        u32 entries = 0;
        ThemeDeleteResult result = ThemeDeleteResult::Ok;
        while (depth >= 0)
        {
            if (f_readdir(&job.dirs[depth], &job.info) != FR_OK)
            {
                result = ThemeDeleteResult::ReadError;
                break;
            }
            if (job.info.fname[0] == 0)
            {
                // this folder is done, back to its parent
                f_closedir(&job.dirs[depth]);
                depth--;
                if (depth >= 0)
                {
                    job.path[job.pathLengths[depth]] = 0;
                }
                continue;
            }

            // FatFs skips "." and "..". A leading dot is fine inside a theme:
            // ._ and .DS_Store files are ordinary files to delete.
            const char* name = job.info.fname;
            if (!ThemeFolderRules::IsSafeName(name, true))
            {
                result = ThemeDeleteResult::BadName;
                break;
            }
            if (job.info.fattrib & AM_RDO)
            {
                result = ThemeDeleteResult::ReadOnly;
                break;
            }
            if (++entries > MAX_ENTRIES)
            {
                result = ThemeDeleteResult::TooManyEntries;
                break;
            }

            u32 childLength = job.pathLengths[depth];
            if (!AppendSegment(job.path, childLength, name))
            {
                result = ThemeDeleteResult::PathTooLong;
                break;
            }

            if (!(job.info.fattrib & AM_DIR))
            {
                // A file whose data starts where a folder does is a damaged
                // card: deleting it would free that folder. Empty files have
                // no cluster.
                DWORD fileCluster = job.info.fclust;
                bool sharesFolder = fileCluster != 0
                    && (fileCluster == themesCluster || fileCluster == picoCluster);
                for (int i = 0; i <= depth; i++)
                {
                    sharesFolder = sharesFolder || (fileCluster != 0 && job.clusters[i] == fileCluster);
                }
                if (sharesFolder)
                {
                    result = ThemeDeleteResult::ReadError;
                    break;
                }
                counts.files++;
                job.path[job.pathLengths[depth]] = 0;
                continue;
            }

            DWORD childCluster = job.info.fclust;
            if (depth + 1 > MAX_DEPTH)
            {
                result = ThemeDeleteResult::TooDeep;
                break;
            }
            bool loops = childCluster == 0;
            for (int i = 0; i <= depth; i++)
            {
                loops = loops || job.clusters[i] == childCluster;
            }
            if (loops)
            {
                // a folder with no cluster, or one that is its own ancestor:
                // the card is damaged, and deleting could reach anything
                result = ThemeDeleteResult::ReadError;
                break;
            }

            depth++;
            if (f_opendir(&job.dirs[depth], job.path) != FR_OK)
            {
                depth--;
                result = ThemeDeleteResult::ReadError;
                break;
            }
            if (job.dirs[depth].obj.sclust != childCluster)
            {
                // the name opened another folder through its 8.3 alias
                f_closedir(&job.dirs[depth]);
                depth--;
                result = ThemeDeleteResult::NotFound;
                break;
            }
            if (childCluster == themesCluster || childCluster == picoCluster
                || !IsChildOf(job, childLength, job.clusters[depth - 1]))
            {
                // its ".." does not lead back here: a damaged link to a folder
                // that lives somewhere else on the card
                f_closedir(&job.dirs[depth]);
                depth--;
                result = ThemeDeleteResult::ReadError;
                break;
            }
            job.clusters[depth] = childCluster;
            job.pathLengths[depth] = childLength;
            counts.folders++;
        }

        for (; depth >= 0; depth--)
        {
            f_closedir(&job.dirs[depth]);
        }
        return result;
    }
}

namespace
{
    /// Pass 1 on an allocated job: everything Check does. Leaves the folder's
    /// start cluster in `cluster` when it returns Ok.
    ThemeDeleteResult CheckJob(Job& job, const char* folderName, const char* activeTheme,
        ThemeDeleteCounts& counts, DWORD& cluster)
    {
        counts = ThemeDeleteCounts();
        if (!ThemeFolderRules::IsSafeName(folderName, false))
        {
            return ThemeDeleteResult::BadName;
        }
        // A setting that isn't a plain name can open a folder no listed name equals.
        if (ThemeFolderRules::IsProtected(folderName) || !ThemeFolderRules::IsSafeName(activeTheme, false)
            || strcasecmp(folderName, activeTheme) == 0)
        {
            return ThemeDeleteResult::Protected;
        }

        ThemeDeleteResult result = FindThemeFolder(job, folderName, cluster);
        if (result != ThemeDeleteResult::Ok)
        {
            return result;
        }

        // Never a parent, and never a protected theme under another name.
        DWORD themesCluster = GetFolderCluster(job.info, THEMES_PATH);
        DWORD picoCluster = GetFolderCluster(job.info, "/_pico");
        if (themesCluster == 0 || picoCluster == 0)
        {
            return ThemeDeleteResult::ReadError;
        }
        if (cluster == themesCluster || cluster == picoCluster
            || IsSameFolder(job, "material", cluster) || IsSameFolder(job, "raspberry", cluster)
            || IsSameFolder(job, activeTheme, cluster))
        {
            return ThemeDeleteResult::Protected;
        }

        return WalkThemeFolder(job, folderName, cluster, themesCluster, picoCluster, counts);
    }

    /// True for the theme folder itself, or anything inside it; nothing else
    /// is ever handed to f_unlink.
    bool IsDeletablePath(const Job& job, u32 rootLength)
    {
        if (strncmp(job.path, job.root, rootLength) != 0)
        {
            return false;
        }
        return job.path[rootLength] == 0
            || (job.path[rootLength] == '/' && job.path[rootLength + 1] != 0);
    }

    /// f_unlink with the path checked first. f_unlink removes the entry before
    /// it frees the data, so it can change the card and still fail: the attempt
    /// itself counts as having removed something.
    FRESULT UnlinkChecked(Job& job, u32 rootLength, bool& removedSomething)
    {
        if (!IsDeletablePath(job, rootLength))
        {
            return FR_DENIED;
        }
        removedSomething = true;
        return f_unlink(job.path);
    }
}

ThemeDeleteResult ThemeFolderDeleter::Delete(const char* folderName, const char* activeTheme, bool& removedSomething)
{
    removedSomething = false;
    std::unique_ptr<Job, JobDeleter> job(static_cast<Job*>(malloc(sizeof(Job))));
    if (!job)
    {
        return ThemeDeleteResult::OutOfMemory;
    }

    ThemeDeleteCounts counts;
    DWORD rootCluster = 0;
    ThemeDeleteResult result = CheckJob(*job, folderName, activeTheme, counts, rootCluster);
    if (result != ThemeDeleteResult::Ok)
    {
        return result;
    }

    u32 rootLength;
    if (!SetThemePath(job->root, rootLength, folderName) || !SetThemePath(job->path, rootLength, folderName))
    {
        return ThemeDeleteResult::PathTooLong;
    }

    // FatFs refuses to remove the current folder, and here that is whatever
    // folder the browser was in. Every path the launcher uses is absolute.
    if (f_chdir("/") != FR_OK)
    {
        return ThemeDeleteResult::ReadError;
    }

    // theme.json first, by its name on the card, so a folder a failure leaves
    // behind is a blank row and never a half theme.
    DIR& rootDir = job->dirs[0];
    if (f_opendir(&rootDir, job->root) != FR_OK)
    {
        return ThemeDeleteResult::ReadError;
    }
    if (rootDir.obj.sclust != rootCluster)
    {
        f_closedir(&rootDir);
        return ThemeDeleteResult::NotFound;
    }
    FRESULT readResult;
    bool foundThemeJson = false;
    while ((readResult = f_readdir(&rootDir, &job->info)) == FR_OK && job->info.fname[0] != 0)
    {
        if (!(job->info.fattrib & AM_DIR) && strcasecmp(job->info.fname, "theme.json") == 0)
        {
            foundThemeJson = true;
            break;
        }
    }
    f_closedir(&rootDir);
    if (readResult != FR_OK)
    {
        return ThemeDeleteResult::ReadError;
    }
    if (foundThemeJson)
    {
        u32 length = rootLength;
        if (!AppendSegment(job->path, length, job->info.fname))
        {
            return ThemeDeleteResult::PathTooLong;
        }
        if (UnlinkChecked(*job, rootLength, removedSomething) != FR_OK)
        {
            return ThemeDeleteResult::WriteError;
        }
        job->path[rootLength] = 0;
    }

    // Then the rest: open the current folder, take its first entry, delete it
    // or go into it; a folder that reads empty is deleted and left. Each pass
    // opens the folder afresh, so nothing depends on how FatFs reads a folder
    // it is deleting from. The walk above counted everything, so the loop is
    // capped a little past that.
    job->clusters[0] = rootCluster;
    job->pathLengths[0] = rootLength;
    int depth = 0;
    u32 iterationsLeft = (counts.files + counts.folders) * 2 + 8;
    while (iterationsLeft-- > 0)
    {
        DIR& dir = job->dirs[0];
        if (f_opendir(&dir, job->path) != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }
        if (dir.obj.sclust != job->clusters[depth])
        {
            // the path no longer opens the folder that was walked
            f_closedir(&dir);
            return ThemeDeleteResult::NotFound;
        }
        readResult = f_readdir(&dir, &job->info);
        f_closedir(&dir);
        if (readResult != FR_OK)
        {
            return ThemeDeleteResult::ReadError;
        }

        if (job->info.fname[0] == 0)
        {
            // this folder is empty: delete it, and go back to its parent
            if (UnlinkChecked(*job, rootLength, removedSomething) != FR_OK)
            {
                return ThemeDeleteResult::WriteError;
            }
            if (depth == 0)
            {
                return ThemeDeleteResult::Ok;
            }
            depth--;
            job->path[job->pathLengths[depth]] = 0;
            continue;
        }

        const char* name = job->info.fname;
        if (!ThemeFolderRules::IsSafeName(name, true))
        {
            return ThemeDeleteResult::BadName;
        }
        if (job->info.fattrib & AM_RDO)
        {
            return ThemeDeleteResult::ReadOnly;
        }
        u32 childLength = job->pathLengths[depth];
        if (!AppendSegment(job->path, childLength, name))
        {
            return ThemeDeleteResult::PathTooLong;
        }

        if (job->info.fattrib & AM_DIR)
        {
            DWORD childCluster = job->info.fclust;
            bool loops = childCluster == 0 || depth + 1 > MAX_DEPTH;
            for (int i = 0; i <= depth; i++)
            {
                loops = loops || job->clusters[i] == childCluster;
            }
            if (loops)
            {
                return ThemeDeleteResult::ReadError;
            }
            depth++;
            job->clusters[depth] = childCluster;
            job->pathLengths[depth] = childLength;
            continue;
        }

        if (UnlinkChecked(*job, rootLength, removedSomething) != FR_OK)
        {
            return ThemeDeleteResult::WriteError;
        }
        job->path[job->pathLengths[depth]] = 0;
    }

    // more passes than the folder had entries: stop rather than guess
    return ThemeDeleteResult::TooManyEntries;
}
