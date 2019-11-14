#include <iostream>

#include <sdkddkver.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Unknwnbase.h>

#include <winrt/base.h>

#include <array>
#include <tuple>
#include <functional>

#include <d3d11_4.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include "renderdoc_app.h"

//#define FORCE_WARP

#define RDOC_CAPTURE_DX11
// #define RDOC_CAPTURE_DX12

using namespace DirectX;

winrt::com_ptr<ID3D11Device5> CreateD3D11Device() {
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_12_1,
                                         D3D_FEATURE_LEVEL_12_0,
                                         D3D_FEATURE_LEVEL_11_1,
                                         D3D_FEATURE_LEVEL_11_0,
                                         D3D_FEATURE_LEVEL_10_1,
                                         D3D_FEATURE_LEVEL_10_0};

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;

#ifdef FORCE_WARP
    const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_WARP;
#else
    const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
#endif

    D3D_FEATURE_LEVEL d3dFeatureLevel;
    winrt::check_hresult(D3D11CreateDevice(nullptr,
                                           driverType,
                                           0,
                                           creationFlags,
                                           featureLevels,
                                           static_cast<UINT>(std::size(featureLevels)),
                                           D3D11_SDK_VERSION,
                                           device.put(),
                                           &d3dFeatureLevel,
                                           context.put()));

    return device.as<ID3D11Device5>();
}

winrt::com_ptr<ID3D12Device> CreateD3D12Device() {
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    winrt::com_ptr<ID3D12Debug> debugCtrl;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug, debugCtrl.put_void()))) {
        debugCtrl->EnableDebugLayer();

        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    winrt::com_ptr<IDXGIFactory4> dxgiFactory;
    winrt::check_hresult(CreateDXGIFactory2(dxgiFactoryFlags, IID_IDXGIFactory4, dxgiFactory.put_void()));

    winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
#ifdef FORCE_WARP
    dxgiFactory->EnumWarpAdapter(winrt::guid_of<IDXGIAdapter>(), dxgiAdapter.put_void());
#endif

    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::check_hresult(
        D3D12CreateDevice(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0, winrt::guid_of<ID3D12Device>(), d3d12Device.put_void()));

    return d3d12Device;
}

void D3D12ForceFinish(ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    winrt::com_ptr<ID3D12Fence> finishFence;
    uint64_t finishFenceValue{0};
    winrt::check_hresult(
        device->CreateFence(finishFenceValue, D3D12_FENCE_FLAG_NONE, winrt::guid_of<ID3D12Fence>(), finishFence.put_void()));
    winrt::check_hresult(cmdQueue->Signal(finishFence.get(), ++finishFenceValue));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    winrt::check_hresult(finishFence->SetEventOnCompletion(finishFenceValue, fenceEvent));
    const uint32_t retVal = WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE);
    switch (retVal) {
    case WAIT_OBJECT_0:
        break;

    default:
        winrt::check_hresult(E_FAIL);
    }
    CloseHandle(fenceEvent);
}

std::tuple<winrt::com_ptr<ID3D11Texture2D>, winrt::com_ptr<ID3D12Resource>, winrt::com_ptr<ID3D11Texture2D>>
CreateTextureArray(ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device) {
    D3D12_RESOURCE_DESC d3d12TextureDesc{};
    d3d12TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d3d12TextureDesc.Alignment = 0;
    d3d12TextureDesc.Width = 256;
    d3d12TextureDesc.Height = 256;
    d3d12TextureDesc.DepthOrArraySize = 2;
    d3d12TextureDesc.MipLevels = 1;
    d3d12TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d3d12TextureDesc.SampleDesc.Count = 1;
    d3d12TextureDesc.SampleDesc.Quality = 0;
    d3d12TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d3d12TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 0;
    heapProperties.VisibleNodeMask = 0;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = d3d12TextureDesc.Format;

    winrt::com_ptr<ID3D12Resource> d3d12Texture;
    winrt::check_hresult(d3d12Device->CreateCommittedResource(&heapProperties,
                                                              D3D12_HEAP_FLAG_SHARED,
                                                              &d3d12TextureDesc,
                                                              D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                              &clearValue,
                                                              winrt::guid_of<ID3D12Resource>(),
                                                              d3d12Texture.put_void()));

    HANDLE sharedHandle;
    winrt::check_hresult(d3d12Device->CreateSharedHandle(d3d12Texture.get(), nullptr, GENERIC_ALL, nullptr, &sharedHandle));

    winrt::com_ptr<ID3D11Texture2D> sharedD3d11Texture;
    winrt::check_hresult(d3d11Device->OpenSharedResource1(sharedHandle, winrt::guid_of<ID3D11Texture2D>(), sharedD3d11Texture.put_void()));

    // Create another from dx11
    winrt::com_ptr<ID3D11Texture2D> d3d11Texture;
    D3D11_TEXTURE2D_DESC dx11TexDesc;
    sharedD3d11Texture->GetDesc(&dx11TexDesc);
    winrt::check_hresult(d3d11Device->CreateTexture2D(&dx11TexDesc, nullptr, d3d11Texture.put()));

    return {sharedD3d11Texture, d3d12Texture, d3d11Texture};
}

void FillTextureArray(ID3D12Device* d3d12Device,
                      ID3D12GraphicsCommandList* d3d12CmdList,
                      ID3D12Resource* d3d12Texture,
                      ID3D11Texture2D* d3d11Texture,
                      const XMFLOAT4 subresColors[2]) {
    // Fill subresColors to d3d12 natively created texture
    D3D12_RESOURCE_DESC d3d12TextureDesc = d3d12Texture->GetDesc();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 3;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;
    winrt::check_hresult(d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, winrt::guid_of<ID3D12DescriptorHeap>(), rtvHeap.put_void()));

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const uint32_t rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
    rtvDesc.Format = d3d12TextureDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Texture2DArray.MipSlice = 0;
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.PlaneSlice = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
    for (uint32_t subres = 0; subres < 2; ++subres) {
        rtvDesc.Texture2DArray.FirstArraySlice = subres;
        rtvHandles[subres].ptr = rtvHandleStart.ptr + subres * rtvDescriptorSize;

        d3d12Device->CreateRenderTargetView(d3d12Texture, &rtvDesc, rtvHandles[subres]);
    }

    for (uint32_t subres = 0; subres < 2; ++subres) {
        d3d12CmdList->ClearRenderTargetView(rtvHandles[subres], &subresColors[subres].x, 0, nullptr);
    }

    // Fill the same data to d3d11 natively created texture
    D3D11_RENDER_TARGET_VIEW_DESC rtvDescDx11;
    D3D11_TEXTURE2D_DESC d3d11TextureDesc;
    winrt::com_ptr<ID3D11RenderTargetView> rtvD3d11;
    d3d11Texture->GetDesc(&d3d11TextureDesc);
    rtvDescDx11.Format = d3d11TextureDesc.Format;
    rtvDescDx11.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDescDx11.Texture2DArray.MipSlice = 0;
    rtvDescDx11.Texture2DArray.ArraySize = 1;

    winrt::com_ptr<ID3D11Device> d3d11Device;
    d3d11Texture->GetDevice(d3d11Device.put());
    winrt::com_ptr<ID3D11DeviceContext> d3d11Context;
    d3d11Device->GetImmediateContext(d3d11Context.put());
    for (uint32_t subres = 0; subres < 2; ++subres) {
        rtvDescDx11.Texture2DArray.FirstArraySlice = subres;
        d3d11Device->CreateRenderTargetView(d3d11Texture, &rtvDescDx11, rtvD3d11.put());
        d3d11Context->ClearRenderTargetView(rtvD3d11.get(), &subresColors[subres].x);

        rtvD3d11 = nullptr;
    }
}

void PrintResult(const std::array<bool, 2>& slice) {
    for (uint32_t subres = 0; subres < 2; ++subres) {
        std::cout << "\tSlice " << subres << " ";
        if (slice[subres]) {
            std::cout << "succeeded!";
        } else {
            std::cout << "FAILED!!!";
        }
        std::cout << "\n";
    }
}

std::array<bool, 2> TryDirectlyCopyFromD3D12ToD3D12(ID3D12Device* d3d12Device,
                                                    ID3D12CommandQueue* d3d12CmdQueue,
                                                    ID3D12GraphicsCommandList* d3d12CmdList,
                                                    ID3D12CommandAllocator* d3d12CmdAllocator,
                                                    ID3D12Resource* d3d12Texture,
                                                    const uint32_t expectedRgbas[]) {
    std::array<bool, 2> ret;

    for (uint32_t subres = 0; subres < 2; ++subres) {
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = d3d12Texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        D3D12_RESOURCE_DESC colorDesc = d3d12Texture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
        uint32_t numRows = 0;
        uint64_t rowSizeInBytes = 0;
        uint64_t requiredSize = 0;
        d3d12Device->GetCopyableFootprints(&colorDesc, subres, 1, 0, &layout, &numRows, &rowSizeInBytes, &requiredSize);

        D3D12_HEAP_PROPERTIES heap;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = requiredSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        winrt::com_ptr<ID3D12Resource> buffer;
        winrt::check_hresult(d3d12Device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, buffer.put_void()));

        D3D12_BOX srcBox;
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.right = static_cast<UINT>(colorDesc.Width);
        srcBox.bottom = static_cast<UINT>(colorDesc.Height);
        srcBox.back = 1;

        D3D12_TEXTURE_COPY_LOCATION src;
        src.pResource = d3d12Texture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = subres;

        D3D12_TEXTURE_COPY_LOCATION dst;
        dst.pResource = buffer.get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = layout;

        d3d12CmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);

        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        {
            d3d12CmdList->Close();
            ID3D12CommandList* cmdLists[] = {d3d12CmdList};
            d3d12CmdQueue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmdLists)), cmdLists);

            D3D12ForceFinish(d3d12Device, d3d12CmdQueue);

            d3d12CmdAllocator->Reset();
            d3d12CmdList->Reset(d3d12CmdAllocator, nullptr);
        }

        D3D12_RANGE read_range;
        read_range.Begin = 0;
        read_range.End = static_cast<SIZE_T>(requiredSize);

        uint32_t* ptr;
        buffer->Map(0, &read_range, reinterpret_cast<void**>(&ptr));

        ret[subres] = (ptr[0] == expectedRgbas[subres]);

        read_range.End = 0;
        buffer->Unmap(0, &read_range);
    }

    return ret;
}

std::array<bool, 2> TryIntermediateTextureCopyFromD3D12ToD3D11(ID3D11Device5* d3d11Device,
                                                               ID3D12Device* d3d12Device,
                                                               ID3D12CommandQueue* d3d12CmdQueue,
                                                               ID3D12GraphicsCommandList* d3d12CmdList,
                                                               ID3D12CommandAllocator* d3d12CmdAllocator,
                                                               ID3D11Texture2D* d3d11Texture,
                                                               ID3D12Resource* d3d12Texture,
                                                               const uint32_t expectedRgbas[]) {
    std::array<bool, 2> ret;

    D3D12_RESOURCE_DESC sliceTextureDesc = d3d12Texture->GetDesc();
    sliceTextureDesc.DepthOrArraySize = 1;

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    winrt::check_hresult(d3d12Texture->GetHeapProperties(&heapProperties, &heapFlags));

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = d3d12Texture->GetDesc().Format;

    winrt::com_ptr<ID3D12Resource> d3d12SliceTexture;
    winrt::check_hresult(d3d12Device->CreateCommittedResource(&heapProperties,
                                                              D3D12_HEAP_FLAG_SHARED,
                                                              &sliceTextureDesc,
                                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                                              &clearValue,
                                                              winrt::guid_of<ID3D12Resource>(),
                                                              d3d12SliceTexture.put_void()));

    HANDLE sharedHandle;
    winrt::check_hresult(d3d12Device->CreateSharedHandle(d3d12SliceTexture.get(), nullptr, GENERIC_ALL, nullptr, &sharedHandle));

    winrt::com_ptr<ID3D11Texture2D> sharedD3d11SliceTexture;
    winrt::check_hresult(
        d3d11Device->OpenSharedResource1(sharedHandle, winrt::guid_of<ID3D11Texture2D>(), sharedD3d11SliceTexture.put_void()));

    for (uint32_t subres = 0; subres < 2; ++subres) {
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = d3d12Texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION src;
        src.pResource = d3d12Texture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = subres;

        D3D12_TEXTURE_COPY_LOCATION dst;
        dst.pResource = d3d12SliceTexture.get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        d3d12CmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        d3d12CmdList->Close();
        ID3D12CommandList* cmdLists[] = {d3d12CmdList};
        d3d12CmdQueue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmdLists)), cmdLists);

        D3D12ForceFinish(d3d12Device, d3d12CmdQueue);

        d3d12CmdAllocator->Reset();
        d3d12CmdList->Reset(d3d12CmdAllocator, nullptr);

        D3D11_TEXTURE2D_DESC colorDesc;
        d3d11Texture->GetDesc(&colorDesc);
        colorDesc.Usage = D3D11_USAGE_STAGING;
        colorDesc.BindFlags = 0;
        colorDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        colorDesc.MiscFlags = 0;
        winrt::com_ptr<ID3D11Texture2D> capturedCpuColorBuffer;
        winrt::check_hresult(d3d11Device->CreateTexture2D(&colorDesc, nullptr, capturedCpuColorBuffer.put()));

        winrt::com_ptr<ID3D11DeviceContext> deviceContext;
        d3d11Device->GetImmediateContext(deviceContext.put());

        deviceContext->CopySubresourceRegion(capturedCpuColorBuffer.get(), subres, 0, 0, 0, sharedD3d11SliceTexture.get(), 0, nullptr);

        D3D11_MAPPED_SUBRESOURCE mappedRes;
        deviceContext->Map(capturedCpuColorBuffer.get(), subres, D3D11_MAP_READ, 0, &mappedRes);

        const uint32_t* ptr = reinterpret_cast<const uint32_t*>(mappedRes.pData);
        ret[subres] = (ptr[0] == expectedRgbas[subres]);

        deviceContext->Unmap(capturedCpuColorBuffer.get(), subres);
    }

    return ret;
}

std::array<bool, 2> TryDirectlyShareFromD3D12ToD3D11(ID3D11Device5* d3d11Device,
                                                     ID3D11Texture2D* d3d11Texture,
                                                     const uint32_t expectedRgbas[]) {
    std::array<bool, 2> ret;

    D3D11_TEXTURE2D_DESC colorDesc;
    d3d11Texture->GetDesc(&colorDesc);
    colorDesc.Usage = D3D11_USAGE_STAGING;
    colorDesc.BindFlags = 0;
    colorDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    colorDesc.MiscFlags = 0;
    winrt::com_ptr<ID3D11Texture2D> capturedCpuColorBuffer;
    winrt::check_hresult(d3d11Device->CreateTexture2D(&colorDesc, nullptr, capturedCpuColorBuffer.put()));

    winrt::com_ptr<ID3D11DeviceContext> deviceContext;
    d3d11Device->GetImmediateContext(deviceContext.put());
    deviceContext->CopyResource(capturedCpuColorBuffer.get(), d3d11Texture);

    for (uint32_t subres = 0; subres < 2; ++subres) {
        D3D11_MAPPED_SUBRESOURCE mappedRes;
        deviceContext->Map(capturedCpuColorBuffer.get(), subres, D3D11_MAP_READ, 0, &mappedRes);

        const uint32_t* ptr = reinterpret_cast<const uint32_t*>(mappedRes.pData);
        ret[subres] = (ptr[0] == expectedRgbas[subres]);

        deviceContext->Unmap(capturedCpuColorBuffer.get(), subres);
    }

    return ret;
}

void TryShareD3D11FenceToD3D12(ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device) {
    // Note: This currently does nothing; just to test if renderdoc can OpenSharedHandle on fence
    winrt::com_ptr<ID3D11Fence> fence;
    winrt::check_hresult(d3d11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, winrt::guid_of<ID3D11Fence>(), fence.put_void()));

    HANDLE d3d12FenceSharedFromD3d11;
    {
        winrt::check_hresult(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &d3d12FenceSharedFromD3d11));

        // Try open the handle from d3d12 device
        winrt::com_ptr<ID3D12Fence> d3d12Fence;
        winrt::check_hresult(d3d12Device->OpenSharedHandle(d3d12FenceSharedFromD3d11, __uuidof(ID3D12Fence), d3d12Fence.put_void()));
    }
    CloseHandle(d3d12FenceSharedFromD3d11);
    std::cout << "succeeded!" << std::endl;
}

void TryIUnknownCasting(ID3D11Device* d3d11device, ID3D12Device* d3d12device) {
    std::cout << "Try back casting d3d12 device from IUnknown" << std::endl;
    {
        IUnknown* deviceIUnknown = d3d12device;
        winrt::com_ptr<ID3D12Device> deviceBackCast;
        deviceIUnknown->QueryInterface(__uuidof(ID3D12Device), deviceBackCast.put_void());

        if (deviceBackCast.get() != d3d12device) {
            std::cout << "FAILED..." << std::endl;
        } else {
            std::cout << "succeeded!" << std::endl;
        }
    }

    std::cout << "Try back casting d3d11 device from IUnknown" << std::endl;
    {
        IUnknown* deviceIUnknown = d3d11device;
        winrt::com_ptr<ID3D11Device> deviceBackCast;
        deviceIUnknown->QueryInterface(__uuidof(ID3D11Device), deviceBackCast.put_void());

        if (deviceBackCast.get() != d3d11device) {
            std::cout << "FAILED..." << std::endl;
        } else {
            std::cout << "succeeded!" << std::endl;
        }
    }
}

void TryD3D12ImplicitResourceSharing(ID3D12Resource* d3d12Texture) {
    winrt::com_ptr<ID3D12Device> secondD3d12device = CreateD3D12Device();
    winrt::com_ptr<ID3D12CommandQueue> d3d12CmdQueue;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    winrt::check_hresult(secondD3d12device->CreateCommandQueue(&queueDesc, winrt::guid_of<ID3D12CommandQueue>(), d3d12CmdQueue.put_void()));

    winrt::com_ptr<ID3D12CommandAllocator> d3d12CmdAllocator;
    winrt::check_hresult(secondD3d12device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, winrt::guid_of<ID3D12CommandAllocator>(), d3d12CmdAllocator.put_void()));

    winrt::com_ptr<ID3D12GraphicsCommandList> d3d12CmdList;
    winrt::check_hresult(secondD3d12device->CreateCommandList(0,
                                                              D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                              d3d12CmdAllocator.get(),
                                                              nullptr,
                                                              winrt::guid_of<ID3D12GraphicsCommandList>(),
                                                              d3d12CmdList.put_void()));
    d3d12CmdList->Close();

    winrt::check_hresult(d3d12CmdAllocator->Reset());
    winrt::check_hresult(d3d12CmdList->Reset(d3d12CmdAllocator.get(), nullptr));

    // Try modify the barrier of texture created from the first device
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = d3d12Texture;
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

        barrier.Transition.Subresource = 0;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        barrier.Transition.Subresource = 1;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        {
            d3d12CmdList->Close();
            ID3D12CommandList* cmdLists[] = {d3d12CmdList.get()};
            d3d12CmdQueue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmdLists)), cmdLists);

            D3D12ForceFinish(secondD3d12device.get(), d3d12CmdQueue.get());

            d3d12CmdAllocator->Reset();
            d3d12CmdList->Reset(d3d12CmdAllocator.get(), nullptr);
        }
    }
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        barrier.Transition.Subresource = 0;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        barrier.Transition.Subresource = 1;
        d3d12CmdList->ResourceBarrier(1, &barrier);

        {
            d3d12CmdList->Close();
            ID3D12CommandList* cmdLists[] = {d3d12CmdList.get()};
            d3d12CmdQueue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmdLists)), cmdLists);

            D3D12ForceFinish(secondD3d12device.get(), d3d12CmdQueue.get());

            d3d12CmdAllocator->Reset();
            d3d12CmdList->Reset(d3d12CmdAllocator.get(), nullptr);
        }
    }

    std::cout << "succeeded!\n";
}

void TextureArrayTest(ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device) {
    winrt::com_ptr<ID3D12CommandQueue> d3d12CmdQueue;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    winrt::check_hresult(d3d12Device->CreateCommandQueue(&queueDesc, winrt::guid_of<ID3D12CommandQueue>(), d3d12CmdQueue.put_void()));

    winrt::com_ptr<ID3D12CommandAllocator> d3d12CmdAllocator;
    winrt::check_hresult(d3d12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, winrt::guid_of<ID3D12CommandAllocator>(), d3d12CmdAllocator.put_void()));

    winrt::com_ptr<ID3D12GraphicsCommandList> d3d12CmdList;
    winrt::check_hresult(d3d12Device->CreateCommandList(0,
                                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        d3d12CmdAllocator.get(),
                                                        nullptr,
                                                        winrt::guid_of<ID3D12GraphicsCommandList>(),
                                                        d3d12CmdList.put_void()));
    d3d12CmdList->Close();

    winrt::check_hresult(d3d12CmdAllocator->Reset());
    winrt::check_hresult(d3d12CmdList->Reset(d3d12CmdAllocator.get(), nullptr));

    auto [d3d11TextureSharedFromD3d12, d3d12Texture, d3d11Texture] = CreateTextureArray(d3d11Device, d3d12Device);

    for (uint32_t test = 0; test < 10; ++test) {
        std::cout << "================================== Test " << test << " ==================================\n\n";

        const uint8_t components[] = {
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
            static_cast<uint8_t>(rand() & 0xFF),
        };

        XMFLOAT4 subresColors[2];
        uint32_t subresRgbas[2];
        for (size_t i = 0; i < std::size(subresRgbas); ++i) {
            subresColors[i] = {
                std::min(1.0f, components[i * 4 + 0] / 255.0f),
                std::min(1.0f, components[i * 4 + 1] / 255.0f),
                std::min(1.0f, components[i * 4 + 2] / 255.0f),
                std::min(1.0f, components[i * 4 + 3] / 255.0f),
            };
            subresRgbas[i] =
                (components[i * 4 + 0] << 0) | (components[i * 4 + 1] << 8) | (components[i * 4 + 2] << 16) | (components[i * 4 + 3] << 24);
        }

        FillTextureArray(d3d12Device, d3d12CmdList.get(), d3d12Texture.get(), d3d11Texture.get(), subresColors);

        {
            std::cout << "Directly copy from D3D12 texture to D3D12 texture\n";
            PrintResult(TryDirectlyCopyFromD3D12ToD3D12(
                d3d12Device, d3d12CmdQueue.get(), d3d12CmdList.get(), d3d12CmdAllocator.get(), d3d12Texture.get(), subresRgbas));
            std::cout << "\n";
        }

        {
            std::cout << "Take a intermediate texture to copy to D3D11 texture\n";
            PrintResult(TryIntermediateTextureCopyFromD3D12ToD3D11(d3d11Device,
                                                                   d3d12Device,
                                                                   d3d12CmdQueue.get(),
                                                                   d3d12CmdList.get(),
                                                                   d3d12CmdAllocator.get(),
                                                                   d3d11TextureSharedFromD3d12.get(),
                                                                   d3d12Texture.get(),
                                                                   subresRgbas));
            std::cout << "\n";
        }

        {
            std::cout << "Directly share to D3D11 texture\n";
            PrintResult(TryDirectlyShareFromD3D12ToD3D11(d3d11Device, d3d11TextureSharedFromD3d12.get(), subresRgbas));
            std::cout << "\n";
        }

        {
            std::cout << "Try share D3D11 fence to D3D12 and open from D3D12 device\n";
            TryShareD3D11FenceToD3D12(d3d11Device, d3d12Device);
            std::cout << "\n";
        }

        {
            TryIUnknownCasting(d3d11Device, d3d12Device);
            std::cout << "\n";
        }

        {
            std::cout << "Try modify states of resource created by an irrelevant D3D12 device\n";
            TryD3D12ImplicitResourceSharing(d3d12Texture.get());
            std::cout << "\n";
        }
    }
}

RENDERDOC_API_1_4_0* GetRenderdocAPI() {
    RENDERDOC_API_1_4_0* rdoc_api = nullptr;

    if (HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_4_0, (void**)&rdoc_api);
        assert(ret == 1);
    }

    return rdoc_api;
}

int main() {
    RENDERDOC_API_1_4_0* rdoc = GetRenderdocAPI();

    winrt::com_ptr<ID3D11Device5> d3d11Device = CreateD3D11Device();
    winrt::com_ptr<ID3D12Device> d3d12Device = CreateD3D12Device();

    D3D12_FEATURE_DATA_D3D12_OPTIONS4 optionData;
    d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &optionData, sizeof(optionData));
    std::cout << "SharedResourceCompatibilityTier: " << optionData.SharedResourceCompatibilityTier << "\n\n";

    // Capture on dx11 device
#ifdef RDOC_CAPTURE_DX11
    {
        if (rdoc) {
            rdoc->SetCaptureFilePathTemplate("SharedTextureArray_DX11Device");
            rdoc->StartFrameCapture(d3d11Device.get(), nullptr);
        }

        TextureArrayTest(d3d11Device.get(), d3d12Device.get());

        if (rdoc) {
            rdoc->EndFrameCapture(d3d11Device.get(), nullptr);
        }
    }
#endif

#ifdef RDOC_CAPTURE_DX12
    // Capture on dx12 device
    {
        if (rdoc) {
            rdoc->SetCaptureFilePathTemplate("SharedTextureArray_DX12Device");
            rdoc->StartFrameCapture(d3d12Device.get(), nullptr);
        }

        TextureArrayTest(d3d11Device.get(), d3d12Device.get());

        if (rdoc) {
            rdoc->EndFrameCapture(d3d12Device.get(), nullptr);
        }
    }
#endif
}
