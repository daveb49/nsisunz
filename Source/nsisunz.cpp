// Unicode NSIS support by Gringoloco023, http://portableapps.com/node/21879, Februari 6 2010
// Improvements by past-due, https://github.com/past-due/, 2018+
// Timestamp fixups by daveb, now extracts like 7zip preserving modified times, Nov 2025

#ifndef UNICODE
#error "This plugin only supports UNICODE NSIS builds."
#endif
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include <io.h>			// for _get_osfhandle

#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

#include <time.h>
#include <stdint.h>
#include <algorithm> // for std::min

#include "nsis/pluginapi.h"

#ifdef _MSC_VER
#pragma comment(lib, "pluginapi-amd64-unicode.lib")
#endif
#pragma comment(lib,"Shlwapi.lib")

#include "minizip-ng/mz.h"
#include "minizip-ng/mz_strm.h"
#include "minizip-ng/mz_strm_mem.h"
#include "minizip-ng/mz_zip.h"
#include "minizip-ng/mz_zip_rw.h"
#include <shlwapi.h> // for PathRemoveFileSpec

static HWND g_hwndParent = NULL;
static HWND g_hListView = NULL;

void internal_unzip(int uselog);

void doMKDir(const TCHAR* dir) {
	if (!dir || !*dir)
		return;

	TCHAR path[MAX_PATH];
	lstrcpyn(path, dir, MAX_PATH);

	// Normalize slashes
	for (TCHAR* p = path; *p; ++p)
		if (*p == _T('/')) *p = _T('\\');

	// Skip drive letter or UNC root
	TCHAR* p = path;
	if (path[0] == _T('\\') && path[1] == _T('\\')) {
		// Skip \\server\share\ ...
        p = path + 2;
		int slashCount = 0;
		while (*p && slashCount < 2) {
			if (*p == _T('\\'))
				slashCount++;
			++p;
		}
	}
	else if (path[1] == _T(':'))
		p = path + 3; // skip "C:\"

	for (; *p; ++p) {
		if (*p == _T('\\')) {
			*p = 0;
			CreateDirectory(path, NULL);
			*p = _T('\\');
		}
	}
	CreateDirectory(path, NULL);
}

extern "C" __declspec(dllexport)
void Unzip(HWND hwndParent, int string_size, TCHAR* variables, stack_t** stacktop) {
	EXDLL_INIT();
	g_hwndParent = hwndParent;
	g_stacktop = stacktop;		// Set the global stack pointer for popstring/pushstring
	internal_unzip(0);			// silent
}

extern "C" __declspec(dllexport)
void UnzipToLog(HWND hwndParent, int string_size, TCHAR* variables, stack_t** stacktop) {
	EXDLL_INIT();
	g_hwndParent = hwndParent;
	g_stacktop = stacktop;
	internal_unzip(1);			// log each file via DetailPrint
}

// Converts wide string (TCHAR*) to UTF-8 encoded ANSI char*
static char* _T2A(const TCHAR* wideStr) {
	static char buf[1024];
	if (!wideStr) { buf[0] = 0; return buf; }

	// Determine required buffer size
	int len = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, NULL, 0, NULL, NULL);
	if (len > (int)sizeof(buf)) len = sizeof(buf) - 1;

	WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, buf, len, NULL, NULL);
	buf[len] = 0;
	return buf;
}

// Converts UTF-8 ANSI char* to wide string (TCHAR*)
static TCHAR* _A2T(const char* ansiStr) {
	static TCHAR buf[1024];
	if (!ansiStr) { buf[0] = 0; return buf; }

	int len = MultiByteToWideChar(CP_UTF8, 0, ansiStr, -1, NULL, 0);
	if (len > (int)(sizeof(buf) / sizeof(TCHAR))) len = (sizeof(buf) / sizeof(TCHAR)) - 1;

	MultiByteToWideChar(CP_UTF8, 0, ansiStr, -1, buf, len);
	buf[len] = 0;
	return buf;
}

// Callback for EnumChildWindows to find SysListView32
static BOOL CALLBACK FindListViewProc(HWND hwnd, LPARAM lParam)
{
	TCHAR className[64];
	GetClassName(hwnd, className, 64);
	if (_tcscmp(className, _T("SysListView32")) == 0)
	{
		*(HWND*)lParam = hwnd;
		return FALSE; // stop enumeration
	}
	return TRUE;
}

// Finds the Details listview window once
static HWND g_hwndListView = NULL;

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
	TCHAR clsName[64];
	GetClassName(hwnd, clsName, _countof(clsName));
	if (_tcscmp(clsName, _T("SysListView32")) == 0)
	{
		g_hwndListView = hwnd;
		return FALSE; // stop enumeration
	}
	return TRUE; // continue
}

static HWND FindListView(HWND parent) {
	g_hwndListView = NULL;
	EnumChildWindows(parent, EnumChildProc, 0);
	return g_hwndListView;
}

static void DetailPrint(const TCHAR* msg) {
	if (!msg || !g_hwndParent) return;

	if (!g_hwndListView) {
		g_hwndListView = FindListView(g_hwndParent);
		if (!g_hwndListView) {
			// Details window may not exist yet
			// MessageBox(NULL, _T("SysListView32 not found!"), _T("DetailPrint"), MB_OK);
			return;
		}
	}

	//	TCHAR dbg[1024];
	//	wsprintf(dbg, TEXT("Inside DetailPrint about to SendMessage to g_hwndListView HWND: 0x%p, [%s]"), g_hwndListView, msg);
	//	MessageBox(NULL, dbg, TEXT("DetailPrint in plugin debug"), MB_OK);

	LVITEM lvi = {};
	lvi.mask = LVIF_TEXT;
	lvi.iItem = ListView_GetItemCount(g_hwndListView); // append
	lvi.iSubItem = 0;
	lvi.pszText = (LPTSTR)msg;

	ListView_InsertItem(g_hwndListView, &lvi);
	ListView_EnsureVisible(g_hwndListView, lvi.iItem, FALSE);
}

void ReplaceChar(TCHAR* str, TCHAR find, TCHAR replace) {
    for (TCHAR* p = str; (p = _tcschr(p, find)) != NULL; ++p) {
        *p = replace;
    }
}

FILE* log_fp = NULL;

static void LogDebug(const char* fmt, ...)
{
    if (!log_fp) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    fflush(log_fp);
    va_end(args);
}
bool TimeT_ToFileTime_ExplorerWillShowLocal(time_t mtime, FILETIME& outFtUtc) {
    // Convert input epoch (UTC) to a broken-down LOCAL TIME (struct tm)
    std::tm tmLocal{};
    if (localtime_s(&tmLocal, &mtime) != 0)
        return false;

    // Build SYSTEMTIME in LOCAL TIME
    SYSTEMTIME stLocal{};
    stLocal.wYear = tmLocal.tm_year + 1900;
    stLocal.wMonth = tmLocal.tm_mon + 1;
    stLocal.wDay = tmLocal.tm_mday;
    stLocal.wHour = tmLocal.tm_hour;
    stLocal.wMinute = tmLocal.tm_min;
    stLocal.wSecond = tmLocal.tm_sec;

    // Convert SYSTEMTIME (local) -> FILETIME (local)
    FILETIME ftLocal{};
    if (!SystemTimeToFileTime(&stLocal, &ftLocal))
        return false;

    // Convert local FILETIME -> UTC FILETIME (THIS DOES THE DST MAGIC)
    if (!LocalFileTimeToFileTime(&ftLocal, &outFtUtc))
        return false;

    return true;
}

void ApplyZipTimestamp(FILE* fp, const mz_zip_file* file_info)
{
	if (!fp || !file_info)
		return;

	LogDebug("=== ApplyZipTimestamp ===\n");

	uint32_t mtime = file_info->modified_date;
	LogDebug("Using modified_date: %d = 0x%08X\n", mtime, mtime);

	FILETIME ft;
	if (!TimeT_ToFileTime_ExplorerWillShowLocal((time_t)mtime, ft)) {
		LogDebug("Failed to convert DOS time\n");
	} else {
		LogDebug("mtime->FILETIME=0x%08I64X\n", *((unsigned __int64*)&ft));

		if (SetFileTime((HANDLE)_get_osfhandle(_fileno(fp)), nullptr, nullptr, &ft))
			LogDebug("SetFileTime succeeded (DOS)\n");
		else
			LogDebug("SetFileTime failed (DOS)\n");
	}

	LogDebug("=========================\n");
}


void internal_unzip(int uselog) {
    TCHAR zipPath[MAX_PATH];
    if (popstring(zipPath)) {
        pushstring(_T("Error reading ZIP file path"));
        return;
    }

    TCHAR destPath[MAX_PATH];
    if (popstring(destPath)) {
        pushstring(_T("Error reading destination directory path"));
        return;
    }

    void* reader = mz_zip_reader_create();
    if (mz_zip_reader_open_file(reader, _T2A(zipPath)) != MZ_OK) {
        mz_zip_reader_delete(&reader);
        pushstring(_T("Error opening ZIP file"));
        return;
    }

    if (mz_zip_reader_goto_first_entry(reader) != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        pushstring(_T("ZIP file has no entries"));
        return;
    }

    // Allocate 1 MB buffer on heap once, reuse for all files unpacked from the zip
    uint8_t* buffer = (uint8_t*)malloc(1024 * 1024);
    if (!buffer) {
        pushstring(_T("Failed to allocate extraction buffer"));
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return;
    }

	_tfopen_s(&log_fp, _T("xxx_nsisunz_debug.log"), _T("w"));

    int filecount = 0;
	do {
	    mz_zip_file* file_info = nullptr;
	    if (mz_zip_reader_entry_get_info(reader, &file_info) != MZ_OK || !file_info)
	        continue;

	    // Convert filename to TCHAR
	    TCHAR entryName[MAX_PATH];
	    lstrcpyn(entryName, _A2T(file_info->filename), MAX_PATH);
        ReplaceChar(entryName, _T('/'), _T('\\'));

        // Skip directory entries
        if (file_info->uncompressed_size == 0 && _tcschr(entryName, _T('/')) && entryName[_tcslen(entryName) - 1] == _T('/')) {
            continue;
        }
		// Build full output path
	    TCHAR outFile[MAX_PATH];
	    lstrcpyn(outFile, destPath, MAX_PATH);
        if (outFile[_tcslen(outFile) - 1] != _T('\\'))
            _tcscat(outFile, _T("\\"));
	    _tcscat(outFile, entryName);

	    // Ensure directory exists
	    TCHAR dirBuf[MAX_PATH];
	    lstrcpyn(dirBuf, outFile, MAX_PATH);
	    PathRemoveFileSpec(dirBuf);
	    doMKDir(dirBuf);

	    // Open entry and write to disk
	    if (mz_zip_reader_entry_open(reader) == MZ_OK) {
	        FILE* fp = nullptr;
	        _tfopen_s(&fp, outFile, _T("wb"));
	        if (fp) {
	            int32_t bytesRead = 0;
                long totalWritten = 0;
	            do {
	                bytesRead = mz_zip_reader_entry_read(reader, buffer, 1024 * 1024);
                    if (bytesRead > 0) {
                        fwrite(buffer, 1, bytesRead, fp);
                        totalWritten += bytesRead;
                    }
	            } while (bytesRead > 0);

				mz_zip_reader_entry_close(reader);

				// --- Set exact file timestamp ---
				fflush(fp);

				ApplyZipTimestamp(fp, file_info);

				fclose(fp);
				filecount++;

				// wsprintf(msgbuf, _T("Wrote %ld bytes to:\n%s"), totalWritten, outFile);
				// MessageBox(NULL, msgbuf, _T("nsisunz Debug"), MB_OK);
                // Log extraction of this file
	            if (uselog) {
	                TCHAR msg[1024];
	                wsprintf(msg, _T("Extracted: %s"), outFile);
	                DetailPrint(msg);
	            }
			}
        }

	} while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);

	// Free buffer
	delete[] buffer;

	mz_zip_reader_close(reader);
	mz_zip_reader_delete(&reader);

	if (log_fp) fclose(log_fp);

	pushstring(_T("success"));
}
