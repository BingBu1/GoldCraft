// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "goldcraft/resource_stamp.hpp"
#ifdef _WIN32
#include <io.h>
#endif

bool CBaseFileSystem::GoldCraftResourceStamp(const char* name,void* output,unsigned int size)
{
    using goldcraft::filesystem::ResourceStamp;
    if(!name || !*name || !output || size!=sizeof(ResourceStamp))return false;
#ifdef _WIN32
    CFileHandle* file=nullptr;
    // Use precisely the Open(...,"rb",nullptr) winner. Metadata-only pack
    // lookup does not seek its shared stream and does not read member data.
    for(int i=0;i<m_SearchPaths.Count();++i)
    {
        file=reinterpret_cast<CFileHandle*>(FindFile(&m_SearchPaths[i],name,"rb",false,true));
        if(file)break;
    }
    if(!file)return false;
    const bool valid=GoldCraftOpenedResourceStamp(file,output,size);
    // Close intentionally preserves the archive FILE*. This metadata lookup
    // owns its small clone, so delete that clone explicitly without Close.
    if(file->m_bPack)delete file;
    else Close(reinterpret_cast<FileHandle_t>(file));
    return valid;
#else
    return false;
#endif
}

bool CBaseFileSystem::GoldCraftOpenedResourceStamp(void* opened,void* output,unsigned int size)
{
    using goldcraft::filesystem::ResourceStamp;
    if(!opened || !output || size!=sizeof(ResourceStamp))return false;
#ifdef _WIN32
    const auto* file=static_cast<CFileHandle*>(opened);
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    FILE_ID_INFO identity{};
    const auto handle=file->m_pFile?reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file->m_pFile))):INVALID_HANDLE_VALUE;
    const bool valid=handle!=INVALID_HANDLE_VALUE && GetFileType(handle)==FILE_TYPE_DISK &&
        GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic)) &&
        GetFileInformationByHandleEx(handle,FileStandardInfo,&standard,sizeof(standard)) &&
        GetFileInformationByHandleEx(handle,FileIdInfo,&identity,sizeof(identity)) &&
        !standard.Directory && !standard.DeletePending && basic.CreationTime.QuadPart>0 &&
        basic.LastWriteTime.QuadPart>0 && basic.ChangeTime.QuadPart>0 && standard.EndOfFile.QuadPart>=0 &&
        file->m_nStartOffset>=0 && file->m_nLength>=0 &&
        file->m_nStartOffset<=standard.EndOfFile.QuadPart &&
        file->m_nLength<=standard.EndOfFile.QuadPart-file->m_nStartOffset;
    if(valid)
    {
        ResourceStamp stamp{};
        stamp.volume=identity.VolumeSerialNumber;
        Q_memcpy(&stamp.fileLow,identity.FileId.Identifier,8);
        Q_memcpy(&stamp.fileHigh,identity.FileId.Identifier+8,8);
        stamp.size=standard.EndOfFile.QuadPart;stamp.created=basic.CreationTime.QuadPart;
        stamp.modified=basic.LastWriteTime.QuadPart;stamp.changed=basic.ChangeTime.QuadPart;
        stamp.offset=file->m_nStartOffset;stamp.length=file->m_nLength;stamp.packed=file->m_bPack?1:0;
        *static_cast<ResourceStamp*>(output)=stamp;
    }
    return valid;
#else
    return false;
#endif
}
