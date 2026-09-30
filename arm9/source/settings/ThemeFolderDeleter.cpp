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
        FILINFO info;
        DIR dirs[MAX_DEPTH + 1];
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
    ThemeDeleteResult WalkThemeFolder(Job& job, const char* folderName, DWORD rootCluster, ThemeDeleteCounts& counts)
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
                // a file only has to fit in the path buffer
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

ThemeDeleteResult ThemeFolderDeleter::Check(const char* folderName, const char* activeTheme, ThemeDeleteCounts& counts)
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

    std::unique_ptr<Job, JobDeleter> job(static_cast<Job*>(malloc(sizeof(Job))));
    if (!job)
    {
        return ThemeDeleteResult::OutOfMemory;
    }

    DWORD cluster = 0;
    ThemeDeleteResult result = FindThemeFolder(*job, folderName, cluster);
    if (result != ThemeDeleteResult::Ok)
    {
        return result;
    }

    // Never a parent, and never a protected theme under another name.
    if (cluster == GetFolderCluster(job->info, THEMES_PATH) || cluster == GetFolderCluster(job->info, "/_pico")
        || IsSameFolder(*job, "material", cluster) || IsSameFolder(*job, "raspberry", cluster)
        || IsSameFolder(*job, activeTheme, cluster))
    {
        return ThemeDeleteResult::Protected;
    }

    return WalkThemeFolder(*job, folderName, cluster, counts);
}
