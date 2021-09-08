#include "selector.h"

#include "util.h"

#if defined(_WIN32)
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>

int browse_for_directory(const char *prompt, char *output, size_t len) {
    HRESULT hr;
    IFileOpenDialog *pFileOpen;
    WCHAR path[MAX_PATH];
    int ret = -1;

    static int inited = 0;
    if (!inited) {
        CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE);
        inited = 1;
    }

    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void**)&pFileOpen);
    if (!SUCCEEDED(hr)) {
        return -1;
    }

    GetCurrentDirectoryW(MAX_PATH, path);
    {
        LPITEMIDLIST iil;
        IShellItem *pShellItem;
        iil = ILCreateFromPathW(path);
        hr = SHCreateItemFromIDList(iil, &IID_IShellItem, (void **)&pShellItem);
        if (SUCCEEDED(hr)) {
            pFileOpen->lpVtbl->SetFolder(pFileOpen, pShellItem);
        }
        pShellItem->lpVtbl->Release(pShellItem);
        ILFree(iil);
    }
    pFileOpen->lpVtbl->SetOptions(pFileOpen, FOS_PICKFOLDERS);
    hr = pFileOpen->lpVtbl->Show(pFileOpen, NULL);

    if (SUCCEEDED(hr))
    {
        IShellItem *pItem;
        hr = pFileOpen->lpVtbl->GetResult(pFileOpen, &pItem);
        if (SUCCEEDED(hr))
        {
            PWSTR pszFilePath;
            hr = pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath);
            if (SUCCEEDED(hr))
            {
                ret = 0;
                int i;
                WCHAR relpath[MAX_PATH];
                PathRelativePathToW(relpath, path, FILE_ATTRIBUTE_DIRECTORY, pszFilePath, FILE_ATTRIBUTE_DIRECTORY);
                CoTaskMemFree(pszFilePath);
                util_ucs_to_utf8(relpath, output, len);
                output[len - 1] = 0;
                for (i = 0; output[i]; ++i) {
                    if (output[i] == '\\') output[i] = '/';
                }
            }
            pItem->lpVtbl->Release(pItem);
        }
    }
    pFileOpen->lpVtbl->Release(pFileOpen);
    return ret;
}

#endif
