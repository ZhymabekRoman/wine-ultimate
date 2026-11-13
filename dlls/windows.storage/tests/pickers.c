/*
 * WinRT Windows.Storage.Pickers Tests
 *
 * Copyright 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Pickers
#include "windows.storage.h"
#include "windows.storage.pickers.h"

#include "wine/test.h"

static const GUID test_IID_IInitializeWithWindow =
{
    0x3e68d4bd, 0x7135, 0x4d10, { 0x80, 0x18, 0x9f, 0xb6, 0xd9, 0xf3, 0x3f, 0xa1 }
};
static const GUID test_IID_IFileOpenPicker =
{
    0x2ca8278a, 0x12c5, 0x4c5f, { 0x89, 0x77, 0x94, 0x54, 0x77, 0x93, 0xc2, 0x41 }
};
static const GUID test_IID_IFileSavePicker =
{
    0x3286ffcb, 0x617f, 0x4cc5, { 0xaf, 0x6a, 0xb3, 0xfd, 0xf2, 0x9a, 0xd1, 0x45 }
};
static const GUID test_IID_IFolderPicker =
{
    0x084f7799, 0xf3fb, 0x400a, { 0x99, 0xb1, 0x7b, 0x4a, 0x77, 0x2f, 0xd6, 0x0d }
};

static void test_pickers(void)
{
    IActivationFactory *factory = NULL;
    IInspectable *obj = NULL;
    IUnknown *init_win = NULL;
    IFileOpenPicker *open_picker = NULL;
    IFileSavePicker *save_picker = NULL;
    IFolderPicker *folder_picker = NULL;
    __FIVector_1_HSTRING *filters = NULL;
    __FIMap_2_HSTRING___FIVector_1_HSTRING *choices = NULL;
    PickerViewMode view_mode = PickerViewMode_List;
    PickerLocationId loc = PickerLocationId_Unspecified;
    HSTRING hcls, hval, hout;
    UINT32 size = 0;
    boolean replaced = FALSE, found = FALSE;
    HRESULT hr;

    /* FileOpenPicker */
    hr = WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FileOpenPicker,
                              ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FileOpenPicker) - 1, &hcls );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );
    hr = RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( hcls );
    ok( hr == S_OK, "RoGetActivationFactory(FileOpenPicker) failed, hr %#lx\n", hr );

    hr = IActivationFactory_ActivateInstance( factory, &obj );
    ok( hr == S_OK, "ActivateInstance(FileOpenPicker) failed, hr %#lx\n", hr );
    IActivationFactory_Release( factory );

    hr = IInspectable_QueryInterface( obj, &test_IID_IFileOpenPicker, (void **)&open_picker );
    ok( hr == S_OK, "QI IFileOpenPicker failed, hr %#lx\n", hr );
    hr = IInspectable_QueryInterface( obj, &test_IID_IInitializeWithWindow, (void **)&init_win );
    ok( hr == S_OK, "QI IInitializeWithWindow failed, hr %#lx\n", hr );
    if (init_win) IUnknown_Release( init_win );
    IInspectable_Release( obj );

    hr = IFileOpenPicker_put_ViewMode( open_picker, PickerViewMode_Thumbnail );
    ok( hr == S_OK, "put_ViewMode failed, hr %#lx\n", hr );
    hr = IFileOpenPicker_get_ViewMode( open_picker, &view_mode );
    ok( hr == S_OK && view_mode == PickerViewMode_Thumbnail, "get_ViewMode got %d, hr %#lx\n", view_mode, hr );

    hr = IFileOpenPicker_put_SuggestedStartLocation( open_picker, PickerLocationId_Downloads );
    ok( hr == S_OK, "put_SuggestedStartLocation failed, hr %#lx\n", hr );
    hr = IFileOpenPicker_get_SuggestedStartLocation( open_picker, &loc );
    ok( hr == S_OK && loc == PickerLocationId_Downloads, "get_SuggestedStartLocation got %d, hr %#lx\n", loc, hr );

    WindowsCreateString( L"Open Now", 8, &hval );
    hr = IFileOpenPicker_put_CommitButtonText( open_picker, hval );
    ok( hr == S_OK, "put_CommitButtonText failed, hr %#lx\n", hr );
    WindowsDeleteString( hval );
    hr = IFileOpenPicker_get_CommitButtonText( open_picker, &hout );
    ok( hr == S_OK && !wcscmp( WindowsGetStringRawBuffer( hout, NULL ), L"Open Now" ), "CommitButtonText mismatch\n" );
    WindowsDeleteString( hout );

    hr = IFileOpenPicker_get_FileTypeFilter( open_picker, &filters );
    ok( hr == S_OK, "get_FileTypeFilter failed, hr %#lx\n", hr );
    WindowsCreateString( L".png", 4, &hval );
    hr = __FIVector_1_HSTRING_Append( filters, hval );
    ok( hr == S_OK, "Append(.png) failed, hr %#lx\n", hr );
    WindowsDeleteString( hval );
    WindowsCreateString( L".jpg", 4, &hval );
    hr = __FIVector_1_HSTRING_Append( filters, hval );
    ok( hr == S_OK, "Append(.jpg) failed, hr %#lx\n", hr );
    WindowsDeleteString( hval );
    hr = __FIVector_1_HSTRING_get_Size( filters, &size );
    ok( hr == S_OK && size == 2, "got size %u, hr %#lx\n", size, hr );
    hr = __FIVector_1_HSTRING_GetAt( filters, 1, &hout );
    ok( hr == S_OK && !wcscmp( WindowsGetStringRawBuffer( hout, NULL ), L".jpg" ), "GetAt(1) mismatch\n" );
    WindowsDeleteString( hout );
    __FIVector_1_HSTRING_Release( filters );

    /* FileSavePicker */
    hr = WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FileSavePicker,
                              ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FileSavePicker) - 1, &hcls );
    hr = RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( hcls );
    ok( hr == S_OK, "RoGetActivationFactory(FileSavePicker) failed, hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( factory, &obj );
    ok( hr == S_OK, "ActivateInstance(FileSavePicker) failed, hr %#lx\n", hr );
    IActivationFactory_Release( factory );
    hr = IInspectable_QueryInterface( obj, &test_IID_IFileSavePicker, (void **)&save_picker );
    ok( hr == S_OK, "QI IFileSavePicker failed, hr %#lx\n", hr );
    IInspectable_Release( obj );

    WindowsCreateString( L"report.txt", 10, &hval );
    hr = IFileSavePicker_put_SuggestedFileName( save_picker, hval );
    ok( hr == S_OK, "put_SuggestedFileName failed, hr %#lx\n", hr );
    WindowsDeleteString( hval );
    hr = IFileSavePicker_get_SuggestedFileName( save_picker, &hout );
    ok( hr == S_OK && !wcscmp( WindowsGetStringRawBuffer( hout, NULL ), L"report.txt" ), "SuggestedFileName mismatch\n" );
    WindowsDeleteString( hout );

    hr = IFileSavePicker_get_FileTypeChoices( save_picker, &choices );
    ok( hr == S_OK, "get_FileTypeChoices failed, hr %#lx\n", hr );
    hr = IFileOpenPicker_get_FileTypeFilter( open_picker, &filters );
    WindowsCreateString( L"Images", 6, &hval );
    hr = __FIMap_2_HSTRING___FIVector_1_HSTRING_Insert( choices, hval, filters, &replaced );
    ok( hr == S_OK && !replaced, "Insert failed, hr %#lx\n", hr );
    hr = __FIMap_2_HSTRING___FIVector_1_HSTRING_HasKey( choices, hval, &found );
    ok( hr == S_OK && found, "HasKey failed, hr %#lx\n", hr );
    WindowsDeleteString( hval );
    __FIVector_1_HSTRING_Release( filters );
    __FIMap_2_HSTRING___FIVector_1_HSTRING_Release( choices );

    /* FolderPicker */
    hr = WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FolderPicker,
                              ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FolderPicker) - 1, &hcls );
    hr = RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( hcls );
    ok( hr == S_OK, "RoGetActivationFactory(FolderPicker) failed, hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( factory, &obj );
    ok( hr == S_OK, "ActivateInstance(FolderPicker) failed, hr %#lx\n", hr );
    IActivationFactory_Release( factory );
    hr = IInspectable_QueryInterface( obj, &test_IID_IFolderPicker, (void **)&folder_picker );
    ok( hr == S_OK, "QI IFolderPicker failed, hr %#lx\n", hr );
    IInspectable_Release( obj );

    if (getenv( "WINE_STORAGE_TEST_PORTAL" ))
    {
        IAsyncOperation_StorageFile *op_file = NULL;
        IAsyncOperation_IVectorView_StorageFile *op_files = NULL;
        IAsyncOperation_StorageFolder *op_folder = NULL;
        IStorageFile *picked_file = NULL;
        IStorageItem *item = NULL;
        IVectorView_StorageFile *picked_files = NULL;
        IStorageFolder *picked_folder = NULL;
        HSTRING hpath = NULL;

        hr = IFileOpenPicker_PickSingleFileAsync( open_picker, &op_file );
        ok( hr == S_OK, "PickSingleFileAsync failed, hr %#lx\n", hr );
        hr = IAsyncOperation_StorageFile_GetResults( op_file, &picked_file );
        ok( hr == S_OK && picked_file != NULL, "PickSingleFileAsync result failed, hr %#lx, file %p\n", hr, picked_file );
        if (picked_file)
        {
            IStorageFile_QueryInterface( picked_file, &IID_IStorageItem, (void **)&item );
            IStorageItem_get_Path( item, &hpath );
            trace( "Portal PickSingleFileAsync path: %s\n", wine_dbgstr_hstring( hpath ) );
            ok( wcsstr( WindowsGetStringRawBuffer( hpath, NULL ), L"hello world.txt" ) != NULL, "unexpected path\n" );
            WindowsDeleteString( hpath );
            IStorageItem_Release( item );
            IStorageFile_Release( picked_file );
        }
        IAsyncOperation_StorageFile_Release( op_file );

        hr = IFileOpenPicker_PickMultipleFilesAsync( open_picker, &op_files );
        ok( hr == S_OK, "PickMultipleFilesAsync failed, hr %#lx\n", hr );
        hr = IAsyncOperation_IVectorView_StorageFile_GetResults( op_files, &picked_files );
        ok( hr == S_OK && picked_files != NULL, "PickMultipleFilesAsync result failed, hr %#lx\n", hr );
        if (picked_files)
        {
            IVectorView_StorageFile_get_Size( picked_files, &size );
            ok( size == 2, "expected 2 files, got %u\n", size );
            IVectorView_StorageFile_Release( picked_files );
        }
        IAsyncOperation_IVectorView_StorageFile_Release( op_files );

        hr = IFileSavePicker_PickSaveFileAsync( save_picker, &op_file );
        ok( hr == S_OK, "PickSaveFileAsync failed, hr %#lx\n", hr );
        hr = IAsyncOperation_StorageFile_GetResults( op_file, &picked_file );
        ok( hr == S_OK && picked_file != NULL, "PickSaveFileAsync result failed, hr %#lx\n", hr );
        if (picked_file) IStorageFile_Release( picked_file );
        IAsyncOperation_StorageFile_Release( op_file );

        hr = IFolderPicker_PickSingleFolderAsync( folder_picker, &op_folder );
        ok( hr == S_OK, "PickSingleFolderAsync failed, hr %#lx\n", hr );
        hr = IAsyncOperation_StorageFolder_GetResults( op_folder, &picked_folder );
        ok( hr == S_OK && picked_folder != NULL, "PickSingleFolderAsync result failed, hr %#lx\n", hr );
        if (picked_folder) IStorageFolder_Release( picked_folder );
        IAsyncOperation_StorageFolder_Release( op_folder );
    }

    IFileOpenPicker_Release( open_picker );
    IFileSavePicker_Release( save_picker );
    IFolderPicker_Release( folder_picker );
}

START_TEST(pickers)
{
    HRESULT hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );
    test_pickers();
    RoUninitialize();
}
