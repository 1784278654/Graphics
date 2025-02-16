/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;

	//boxes as the castle wall
	GeometryGenerator::MeshData box = geoGen.CreateBox(10.0f, 8.0f, 1.0f, 0);

	//grid as the base for the castle
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 20.0f, 20, 40);

	//spheres that stays in middle of the castle
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);

	//cylinders as the 4 towers
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 1.0f, 4.0f, 20, 20);

	//Cones as the 4 towers top
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 0.0f, 2.0f, 20, 20);

	//Torus for...idk, decoration
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(4.0f, 5.0f, 20, 20);

	//Diamond
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(2.0f, 1.0f, 2.0f);

	//Wedge for the boarding main gate
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(5.0f, 2.0f, 5.0f);

	//pyramids for top of the wall
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 2.0f);

	//triangular prisms for idk decoration as well
	GeometryGenerator::MeshData triangularPrism = geoGen.CreateTriangularPrism(2.0f, 2.0f, 2.0f);

	//quad
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(2.0f, 2.0f, 2.0f, 2.0f, 2.0f);


	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT torusVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT diamondVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
	UINT wedgeVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT pyramidVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT triangularPrismVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT quadVertexOffset = triangularPrismVertexOffset + (UINT)triangularPrism.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT torusIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT diamondIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
	UINT wedgeIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT pyramidIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT triangularPrismIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT quadIndexOffset = triangularPrismIndexOffset + (UINT)triangularPrism.Indices32.size();

	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry triangularPrismSubmesh;
	triangularPrismSubmesh.IndexCount = (UINT)triangularPrism.Indices32.size();
	triangularPrismSubmesh.StartIndexLocation = triangularPrismIndexOffset;
	triangularPrismSubmesh.BaseVertexLocation = triangularPrismVertexOffset;

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		cone.Vertices.size() + 
		torus.Vertices.size() + 
		diamond.Vertices.size() + 
		wedge.Vertices.size() + 
		pyramid.Vertices.size() + 
		triangularPrism.Vertices.size() + 
		quad.Vertices.size();


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}
	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkCyan);
	}
	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGoldenrod);
	}
	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::IndianRed);
	}
	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::BurlyWood);
	}
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SandyBrown);
	}
	for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = triangularPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::OrangeRed);
	}
	for (size_t i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DeepPink);
	}


	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangularPrism.GetIndices16()), std::end(triangularPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["triangularPrism"] = triangularPrismSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
	 mShaders["standardVS"]->GetBufferSize()
	};

	opaquePsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
	 mShaders["opaquePS"]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}



void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}



void ShapesApp::BuildRenderItems()
{
	UINT objIndex = 0;


	//1. 4 boxes as the wall
	//front wall
	auto frontBoxRitem = std::make_unique<RenderItem>();

	XMMATRIX world = XMMatrixRotationY(0) * XMMatrixTranslation(0.0f, 4.0f, 5.0f);
	XMStoreFloat4x4(&frontBoxRitem->World, world);

	frontBoxRitem->ObjCBIndex = objIndex++;
	frontBoxRitem->Geo = mGeometries["shapeGeo"].get();
	frontBoxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontBoxRitem->IndexCount = frontBoxRitem->Geo->DrawArgs["box"].IndexCount;
	frontBoxRitem->StartIndexLocation = frontBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	frontBoxRitem->BaseVertexLocation = frontBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontBoxRitem));
	//back wall
	auto backBoxRitem = std::make_unique<RenderItem>();

	world = XMMatrixRotationY(0) * XMMatrixTranslation(0.0f, 4.0f, -5.0f);
	XMStoreFloat4x4(&backBoxRitem->World, world);

	backBoxRitem->ObjCBIndex = objIndex++;
	backBoxRitem->Geo = mGeometries["shapeGeo"].get();
	backBoxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backBoxRitem->IndexCount = backBoxRitem->Geo->DrawArgs["box"].IndexCount;
	backBoxRitem->StartIndexLocation = backBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	backBoxRitem->BaseVertexLocation = backBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backBoxRitem));
	//left wall
	auto leftBoxRitem = std::make_unique<RenderItem>();

	world = XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(5.0f, 4.0f, 0.0f);
	XMStoreFloat4x4(&leftBoxRitem->World, world);

	leftBoxRitem->ObjCBIndex = objIndex++;
	leftBoxRitem->Geo = mGeometries["shapeGeo"].get();
	leftBoxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftBoxRitem->IndexCount = leftBoxRitem->Geo->DrawArgs["box"].IndexCount;
	leftBoxRitem->StartIndexLocation = leftBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	leftBoxRitem->BaseVertexLocation = leftBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftBoxRitem));
	//right wall
	auto rightBoxRitem = std::make_unique<RenderItem>();

	world = XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(-5.0f, 4.0f, 0.0f);
	XMStoreFloat4x4(&rightBoxRitem->World, world);

	rightBoxRitem->ObjCBIndex = objIndex++;
	rightBoxRitem->Geo = mGeometries["shapeGeo"].get();
	rightBoxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightBoxRitem->IndexCount = rightBoxRitem->Geo->DrawArgs["box"].IndexCount;
	rightBoxRitem->StartIndexLocation = rightBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	rightBoxRitem->BaseVertexLocation = rightBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightBoxRitem));
	

	//2. the grid as a base for the castle
	auto gridRitem = std::make_unique<RenderItem>();
	//world placement
	world = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	XMStoreFloat4x4(&gridRitem->World, world);

	gridRitem->ObjCBIndex = objIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));


	//3. Sphere at the centre
	//down left tower
	auto sphereRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	XMStoreFloat4x4(&sphereRItem->World, world);

	sphereRItem->ObjCBIndex = objIndex++;
	sphereRItem->Geo = mGeometries["shapeGeo"].get();
	sphereRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRItem->IndexCount = sphereRItem->Geo->DrawArgs["sphere"].IndexCount;
	sphereRItem->StartIndexLocation = sphereRItem->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRItem->BaseVertexLocation = sphereRItem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRItem));


	//4. Cylinders as the 4 towers
	//down left tower
	auto downLeftTowerRitem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(-5.0f, 10.0f, 5.0f);
	XMStoreFloat4x4(&downLeftTowerRitem->World, world);

	downLeftTowerRitem->ObjCBIndex = objIndex++;
	downLeftTowerRitem->Geo = mGeometries["shapeGeo"].get();
	downLeftTowerRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	downLeftTowerRitem->IndexCount = downLeftTowerRitem->Geo->DrawArgs["cylinder"].IndexCount;
	downLeftTowerRitem->StartIndexLocation = downLeftTowerRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	downLeftTowerRitem->BaseVertexLocation = downLeftTowerRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(downLeftTowerRitem));

	//down right tower
	auto downRightTowerRitem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(5.0f, 10.0f, 5.0f);
	XMStoreFloat4x4(&downRightTowerRitem->World, world);

	downRightTowerRitem->ObjCBIndex = objIndex++;
	downRightTowerRitem->Geo = mGeometries["shapeGeo"].get();
	downRightTowerRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	downRightTowerRitem->IndexCount = downRightTowerRitem->Geo->DrawArgs["cylinder"].IndexCount;
	downRightTowerRitem->StartIndexLocation = downRightTowerRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	downRightTowerRitem->BaseVertexLocation = downRightTowerRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(downRightTowerRitem));

	//up left tower
	auto upLeftTowerRitem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(-5.0f, 10.0f, -5.0f);
	XMStoreFloat4x4(&upLeftTowerRitem->World, world);

	upLeftTowerRitem->ObjCBIndex = objIndex++;
	upLeftTowerRitem->Geo = mGeometries["shapeGeo"].get();
	upLeftTowerRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	upLeftTowerRitem->IndexCount = upLeftTowerRitem->Geo->DrawArgs["cylinder"].IndexCount;
	upLeftTowerRitem->StartIndexLocation = upLeftTowerRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	upLeftTowerRitem->BaseVertexLocation = upLeftTowerRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(upLeftTowerRitem));

	//up right tower
	auto upRightTowerRitem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(5.0f, 10.0f, -5.0f);
	XMStoreFloat4x4(&upRightTowerRitem->World, world);

	upRightTowerRitem->ObjCBIndex = objIndex++;
	upRightTowerRitem->Geo = mGeometries["shapeGeo"].get();
	upRightTowerRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	upRightTowerRitem->IndexCount = upRightTowerRitem->Geo->DrawArgs["cylinder"].IndexCount;
	upRightTowerRitem->StartIndexLocation = upRightTowerRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	upRightTowerRitem->BaseVertexLocation = upRightTowerRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(upRightTowerRitem));


	//4. cone
	//left down cone
	auto coneRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(-5.0f, 13.0f, 5.0f);
	XMStoreFloat4x4(&coneRItem->World, world);

	coneRItem->ObjCBIndex = objIndex++;
	coneRItem->Geo = mGeometries["shapeGeo"].get();
	coneRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRItem->IndexCount = coneRItem->Geo->DrawArgs["cone"].IndexCount;
	coneRItem->StartIndexLocation = coneRItem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRItem->BaseVertexLocation = coneRItem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRItem));

	//right down cone
	auto rdconeRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(5.0f, 13.0f, 5.0f);
	XMStoreFloat4x4(&rdconeRItem->World, world);

	rdconeRItem->ObjCBIndex = objIndex++;
	rdconeRItem->Geo = mGeometries["shapeGeo"].get();
	rdconeRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rdconeRItem->IndexCount = rdconeRItem->Geo->DrawArgs["cone"].IndexCount;
	rdconeRItem->StartIndexLocation = rdconeRItem->Geo->DrawArgs["cone"].StartIndexLocation;
	rdconeRItem->BaseVertexLocation = rdconeRItem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rdconeRItem));

	//left up cone
	auto luconeRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(-5.0f, 13.0f, -5.0f);
	XMStoreFloat4x4(&luconeRItem->World, world);

	luconeRItem->ObjCBIndex = objIndex++;
	luconeRItem->Geo = mGeometries["shapeGeo"].get();
	luconeRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	luconeRItem->IndexCount = luconeRItem->Geo->DrawArgs["cone"].IndexCount;
	luconeRItem->StartIndexLocation = luconeRItem->Geo->DrawArgs["cone"].StartIndexLocation;
	luconeRItem->BaseVertexLocation = luconeRItem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(luconeRItem));

	//right up cone
	auto ruconeRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(5.0f, 13.0f, -5.0f);
	XMStoreFloat4x4(&ruconeRItem->World, world);

	ruconeRItem->ObjCBIndex = objIndex++;
	ruconeRItem->Geo = mGeometries["shapeGeo"].get();
	ruconeRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ruconeRItem->IndexCount = ruconeRItem->Geo->DrawArgs["cone"].IndexCount;
	ruconeRItem->StartIndexLocation = ruconeRItem->Geo->DrawArgs["cone"].StartIndexLocation;
	ruconeRItem->BaseVertexLocation = ruconeRItem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(ruconeRItem));


	//5. torus
	auto torusRItem = std::make_unique<RenderItem>();

	world = XMMatrixTranslation(0.0f, 2.0f, 0.0f);
	XMStoreFloat4x4(&torusRItem->World, world);

	torusRItem->ObjCBIndex = objIndex++;
	torusRItem->Geo = mGeometries["shapeGeo"].get();
	torusRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torusRItem->IndexCount = torusRItem->Geo->DrawArgs["torus"].IndexCount;
	torusRItem->StartIndexLocation = torusRItem->Geo->DrawArgs["torus"].StartIndexLocation;
	torusRItem->BaseVertexLocation = torusRItem->Geo->DrawArgs["torus"].BaseVertexLocation;
	mAllRitems.push_back(std::move(torusRItem));


	//6. 3 diamond on each wall rooftop
	auto diaRItem = std::make_unique<RenderItem>();
	//left wall
	world = XMMatrixTranslation(-5.0f, 8.5f, 3.0f);
	XMStoreFloat4x4(&diaRItem->World, world);

	diaRItem->ObjCBIndex = objIndex++;
	diaRItem->Geo = mGeometries["shapeGeo"].get();
	diaRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diaRItem->IndexCount = diaRItem->Geo->DrawArgs["diamond"].IndexCount;
	diaRItem->StartIndexLocation = diaRItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diaRItem->BaseVertexLocation = diaRItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diaRItem));

	auto dia1RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(-5.0f, 8.5f, 0.0f);
	XMStoreFloat4x4(&dia1RItem->World, world);

	dia1RItem->ObjCBIndex = objIndex++;
	dia1RItem->Geo = mGeometries["shapeGeo"].get();
	dia1RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia1RItem->IndexCount = dia1RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia1RItem->StartIndexLocation = dia1RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia1RItem->BaseVertexLocation = dia1RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia1RItem));

	auto dia2RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(-5.0f, 8.5f, -3.0f);
	XMStoreFloat4x4(&dia2RItem->World, world);

	dia2RItem->ObjCBIndex = objIndex++;
	dia2RItem->Geo = mGeometries["shapeGeo"].get();
	dia2RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia2RItem->IndexCount = dia2RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia2RItem->StartIndexLocation = dia2RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia2RItem->BaseVertexLocation = dia2RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia2RItem));
	//front wall
	auto dia3RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(-3.0f, 8.5f, 5.0f);
	XMStoreFloat4x4(&dia3RItem->World, world);

	dia3RItem->ObjCBIndex = objIndex++;
	dia3RItem->Geo = mGeometries["shapeGeo"].get();
	dia3RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia3RItem->IndexCount = dia3RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia3RItem->StartIndexLocation = dia3RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia3RItem->BaseVertexLocation = dia3RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia3RItem));

	auto dia4RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 8.5f, 5.0f);
	XMStoreFloat4x4(&dia4RItem->World, world);

	dia4RItem->ObjCBIndex = objIndex++;
	dia4RItem->Geo = mGeometries["shapeGeo"].get();
	dia4RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia4RItem->IndexCount = dia4RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia4RItem->StartIndexLocation = dia4RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia4RItem->BaseVertexLocation = dia4RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia4RItem));

	auto dia5RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(3.0f, 8.5f, 5.0f);
	XMStoreFloat4x4(&dia5RItem->World, world);

	dia5RItem->ObjCBIndex = objIndex++;
	dia5RItem->Geo = mGeometries["shapeGeo"].get();
	dia5RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia5RItem->IndexCount = dia5RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia5RItem->StartIndexLocation = dia5RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia5RItem->BaseVertexLocation = dia5RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia5RItem));

	//back wall
	auto dia6RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(-3.0f, 8.5f, -5.0f);
	XMStoreFloat4x4(&dia6RItem->World, world);

	dia6RItem->ObjCBIndex = objIndex++;
	dia6RItem->Geo = mGeometries["shapeGeo"].get();
	dia6RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia6RItem->IndexCount = dia6RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia6RItem->StartIndexLocation = dia6RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia6RItem->BaseVertexLocation = dia6RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia6RItem));

	auto dia7RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 8.5f, -5.0f);
	XMStoreFloat4x4(&dia7RItem->World, world);

	dia7RItem->ObjCBIndex = objIndex++;
	dia7RItem->Geo = mGeometries["shapeGeo"].get();
	dia7RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia7RItem->IndexCount = dia7RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia7RItem->StartIndexLocation = dia7RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia7RItem->BaseVertexLocation = dia7RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia7RItem));

	auto dia8RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(3.0f, 8.5f, -5.0f);
	XMStoreFloat4x4(&dia8RItem->World, world);

	dia8RItem->ObjCBIndex = objIndex++;
	dia8RItem->Geo = mGeometries["shapeGeo"].get();
	dia8RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia8RItem->IndexCount = dia8RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia8RItem->StartIndexLocation = dia8RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia8RItem->BaseVertexLocation = dia8RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia8RItem));

	//right wall
	auto dia9RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(5.0f, 8.5f, -3.0f);
	XMStoreFloat4x4(&dia9RItem->World, world);

	dia9RItem->ObjCBIndex = objIndex++;
	dia9RItem->Geo = mGeometries["shapeGeo"].get();
	dia9RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia9RItem->IndexCount = dia9RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia9RItem->StartIndexLocation = dia9RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia9RItem->BaseVertexLocation = dia9RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia9RItem));

	auto dia10RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(5.0f, 8.5f, 0.0f);
	XMStoreFloat4x4(&dia10RItem->World, world);

	dia10RItem->ObjCBIndex = objIndex++;
	dia10RItem->Geo = mGeometries["shapeGeo"].get();
	dia10RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia10RItem->IndexCount = dia10RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia10RItem->StartIndexLocation = dia10RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia10RItem->BaseVertexLocation = dia10RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia10RItem));

	auto dia11RItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(5.0f, 8.5f, 3.0f);
	XMStoreFloat4x4(&dia11RItem->World, world);

	dia11RItem->ObjCBIndex = objIndex++;
	dia11RItem->Geo = mGeometries["shapeGeo"].get();
	dia11RItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	dia11RItem->IndexCount = dia11RItem->Geo->DrawArgs["diamond"].IndexCount;
	dia11RItem->StartIndexLocation = dia11RItem->Geo->DrawArgs["diamond"].StartIndexLocation;
	dia11RItem->BaseVertexLocation = dia11RItem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(dia11RItem));


	//7. wedge
	auto wedgeRItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 0.8f, 5.5f);
	XMStoreFloat4x4(&wedgeRItem->World, world);

	wedgeRItem->ObjCBIndex = objIndex++;
	wedgeRItem->Geo = mGeometries["shapeGeo"].get();
	wedgeRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wedgeRItem->IndexCount = wedgeRItem->Geo->DrawArgs["wedge"].IndexCount;
	wedgeRItem->StartIndexLocation = wedgeRItem->Geo->DrawArgs["wedge"].StartIndexLocation;
	wedgeRItem->BaseVertexLocation = wedgeRItem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wedgeRItem));


	//8. pyramid
	auto pyramidRItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 1.5f, 0.0f);
	XMStoreFloat4x4(&pyramidRItem->World, world);

	pyramidRItem->ObjCBIndex = objIndex++;
	pyramidRItem->Geo = mGeometries["shapeGeo"].get();
	pyramidRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRItem->IndexCount = pyramidRItem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRItem->StartIndexLocation = pyramidRItem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRItem->BaseVertexLocation = pyramidRItem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRItem));


	//9. triangularPrism
	auto triPrismRItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 4.5f, 0.0f);
	XMStoreFloat4x4(&triPrismRItem->World, world);

	triPrismRItem->ObjCBIndex = objIndex++;
	triPrismRItem->Geo = mGeometries["shapeGeo"].get();
	triPrismRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triPrismRItem->IndexCount = triPrismRItem->Geo->DrawArgs["triangularPrism"].IndexCount;
	triPrismRItem->StartIndexLocation = triPrismRItem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	triPrismRItem->BaseVertexLocation = triPrismRItem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triPrismRItem));


	//10. quad
	auto quadRItem = std::make_unique<RenderItem>();
	world = XMMatrixTranslation(0.0f, 5.0f, -7.6f);
	XMStoreFloat4x4(&quadRItem->World, world);

	quadRItem->ObjCBIndex = objIndex++;
	quadRItem->Geo = mGeometries["shapeGeo"].get();
	quadRItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadRItem->IndexCount = quadRItem->Geo->DrawArgs["quad"].IndexCount;
	quadRItem->StartIndexLocation = quadRItem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadRItem->BaseVertexLocation = quadRItem->Geo->DrawArgs["quad"].BaseVertexLocation;
	mAllRitems.push_back(std::move(quadRItem));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.

		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}
