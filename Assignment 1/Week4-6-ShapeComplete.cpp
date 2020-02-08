//***************************************************************************************
//create two boxes
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************



#include "d3dApp.h"

#include "MathHelper.h"

#include "UploadBuffer.h"

#include "GeometryGenerator.h"

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

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");

	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");



	mInputLayout =

	{

		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

	};

}



void ShapesApp::BuildShapeGeometry()

{

	GeometryGenerator geoGen;

	GeometryGenerator::MeshData box = geoGen.CreateBox(0.5f, 0.5f, 0.5f, 4);

	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 20.0f, 40, 40);

	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);

	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 5.0f, 20);

	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(0.5f, 0.5f, 0.5f, 4);

	GeometryGenerator::MeshData dome = geoGen.CreateDome(4.0f, 20, 20);

	GeometryGenerator::MeshData tPrism = geoGen.CreateTrianglePrism(1.0f, 2.0f, 4);

	GeometryGenerator::MeshData hPrism = geoGen.CreatePentagonPrism(1.0f, 2.0f, 4);

	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 5, 5);

	//

	// We are concatenating all the geometry into one big vertex/index buffer.  So

	// define the regions in the buffer each submesh covers.

	//



	// Cache the vertex offsets to each object in the concatenated vertex buffer.

	UINT boxVertexOffset = 0;

	UINT gridVertexOffset = (UINT)box.Vertices.size();

	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();

	UINT coneVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	UINT wedgeVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();

	UINT domeVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();

	UINT tPrismVertexOffset = domeVertexOffset + (UINT)dome.Vertices.size();

	UINT hPrismVertexOffset = tPrismVertexOffset + (UINT)tPrism.Vertices.size();

	UINT pyramidVertexOffset = hPrismVertexOffset + (UINT)hPrism.Vertices.size();


	// Cache the starting index for each object in the concatenated index buffer.

	UINT boxIndexOffset = 0;

	UINT gridIndexOffset = (UINT)box.Indices32.size();

	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();

	UINT coneIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	UINT wedgeIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();

	UINT domeIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();

	UINT tPrismIndexOffset = domeIndexOffset + (UINT)dome.Indices32.size();

	UINT hPrismIndexOffset = tPrismIndexOffset + (UINT)tPrism.Indices32.size();

	UINT pyramidIndexOffset = hPrismIndexOffset + (UINT)hPrism.Indices32.size();


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


	SubmeshGeometry coneSubmesh;

	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;


	SubmeshGeometry wedgeSubmesh;

	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;


	SubmeshGeometry domeSubmesh;

	domeSubmesh.IndexCount = (UINT)dome.Indices32.size();
	domeSubmesh.StartIndexLocation = domeIndexOffset;
	domeSubmesh.BaseVertexLocation = domeVertexOffset;


	SubmeshGeometry tPrismSubmesh;

	tPrismSubmesh.IndexCount = (UINT)tPrism.Indices32.size();
	tPrismSubmesh.StartIndexLocation = tPrismIndexOffset;
	tPrismSubmesh.BaseVertexLocation = tPrismVertexOffset;


	SubmeshGeometry hPrismSubmesh;

	hPrismSubmesh.IndexCount = (UINT)hPrism.Indices32.size();
	hPrismSubmesh.StartIndexLocation = hPrismIndexOffset;
	hPrismSubmesh.BaseVertexLocation = hPrismVertexOffset;


	SubmeshGeometry pyramidSubmesh;

	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	//

	// Extract the vertex elements we are interested in and pack the

	// vertices of all the meshes into one vertex buffer.

	//



	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cone.Vertices.size() +
		wedge.Vertices.size() +
		dome.Vertices.size() +
		tPrism.Vertices.size() +
		hPrism.Vertices.size() +
		pyramid.Vertices.size();



	std::vector<Vertex> vertices(totalVertexCount);



	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Black);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Blue);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkBlue);
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gray);
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
	}

	for (size_t i = 0; i < dome.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = dome.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::White);
	}

	for (size_t i = 0; i < tPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = tPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SlateGray);
	}

	for (size_t i = 0; i < hPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = hPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::LightSlateGray);
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::LightBlue);
	}

	std::vector<std::uint16_t> indices;

	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(dome.GetIndices16()), std::end(dome.GetIndices16()));
	indices.insert(indices.end(), std::begin(tPrism.GetIndices16()), std::end(tPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(hPrism.GetIndices16()), std::end(hPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));


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
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["dome"] = domeSubmesh;
	geo->DrawArgs["tPrism"] = tPrismSubmesh;
	geo->DrawArgs["hPrism"] = hPrismSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;


	mGeometries[geo->Name] = std::move(geo);

}



void ShapesApp::BuildPSOs()

{

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;



	//

	// PSO for opaque objects.

	//

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





	//

	// PSO for opaque wireframe objects.

	//



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
	UINT objCBIndex = 0; //

	//-----Plane-----//

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objCBIndex++; //2
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	//-----Dome-----//

	auto domeRitem = std::make_unique<RenderItem>();
	domeRitem->World = MathHelper::Identity4x4();
	domeRitem->ObjCBIndex = objCBIndex++; //2
	domeRitem->Geo = mGeometries["shapeGeo"].get();
	domeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	domeRitem->IndexCount = domeRitem->Geo->DrawArgs["dome"].IndexCount;
	domeRitem->StartIndexLocation = domeRitem->Geo->DrawArgs["dome"].StartIndexLocation;
	domeRitem->BaseVertexLocation = domeRitem->Geo->DrawArgs["dome"].BaseVertexLocation;
	mAllRitems.push_back(std::move(domeRitem));


	//-----Cubes-----//

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(3.0f, 2.0f, 3.0f) * XMMatrixTranslation(-7.5f, 0.5f, 0.0f));
	boxRitem->ObjCBIndex = objCBIndex++;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	auto box2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box2Ritem->World, XMMatrixScaling(3.0f, 2.0f, 3.0f) * XMMatrixTranslation(-7.5f, 0.5f, 3.0f));
	box2Ritem->ObjCBIndex = objCBIndex++;
	box2Ritem->Geo = mGeometries["shapeGeo"].get();
	box2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box2Ritem->IndexCount = box2Ritem->Geo->DrawArgs["box"].IndexCount;
	box2Ritem->StartIndexLocation = box2Ritem->Geo->DrawArgs["box"].StartIndexLocation;
	box2Ritem->BaseVertexLocation = box2Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(box2Ritem));

	auto box3Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box3Ritem->World, XMMatrixScaling(3.0f, 2.0f, 3.0f) * XMMatrixTranslation(7.5f, 0.5f, 0.0f));
	box3Ritem->ObjCBIndex = objCBIndex++;
	box3Ritem->Geo = mGeometries["shapeGeo"].get();
	box3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box3Ritem->IndexCount = box3Ritem->Geo->DrawArgs["box"].IndexCount;
	box3Ritem->StartIndexLocation = box3Ritem->Geo->DrawArgs["box"].StartIndexLocation;
	box3Ritem->BaseVertexLocation = box3Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(box3Ritem));

	auto box4Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box4Ritem->World, XMMatrixScaling(3.0f, 2.0f, 3.0f) * XMMatrixTranslation(9.0f, 0.5f, 9.0f));
	box4Ritem->ObjCBIndex = objCBIndex++;
	box4Ritem->Geo = mGeometries["shapeGeo"].get();
	box4Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box4Ritem->IndexCount = box4Ritem->Geo->DrawArgs["box"].IndexCount;
	box4Ritem->StartIndexLocation = box4Ritem->Geo->DrawArgs["box"].StartIndexLocation;
	box4Ritem->BaseVertexLocation = box4Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(box4Ritem));

	auto box5Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box5Ritem->World, XMMatrixScaling(8.0f, 4.0f, 4.0f) * XMMatrixTranslation(0.0f, 0.5f, -7.5f));
	box5Ritem->ObjCBIndex = objCBIndex++;
	box5Ritem->Geo = mGeometries["shapeGeo"].get();
	box5Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box5Ritem->IndexCount = box5Ritem->Geo->DrawArgs["box"].IndexCount;
	box5Ritem->StartIndexLocation = box5Ritem->Geo->DrawArgs["box"].StartIndexLocation;
	box5Ritem->BaseVertexLocation = box5Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(box5Ritem));

	//-----Wedges-----//

	auto wedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wedgeRitem->World, XMMatrixScaling(4.0f, 4.0f, 10.0f) * XMMatrixRotationY(-XM_PI / 2.0f) * XMMatrixTranslation(4.5f, 0.5f, -7.5f));
	wedgeRitem->ObjCBIndex = objCBIndex++;
	wedgeRitem->Geo = mGeometries["shapeGeo"].get();
	wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wedgeRitem));

	auto wedge2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wedge2Ritem->World, XMMatrixScaling(4.0f, 4.0f, 10.0f) * XMMatrixRotationY(XM_PI / 2.0f) * XMMatrixTranslation(-4.5f, 0.5f, -7.5f));
	wedge2Ritem->ObjCBIndex = objCBIndex++;
	wedge2Ritem->Geo = mGeometries["shapeGeo"].get();
	wedge2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wedge2Ritem->IndexCount = wedge2Ritem->Geo->DrawArgs["wedge"].IndexCount;
	wedge2Ritem->StartIndexLocation = wedge2Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
	wedge2Ritem->BaseVertexLocation = wedge2Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wedge2Ritem));

	//-----Prisms-----//

	auto tPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&tPrismRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-2.0f, 2.0f, 7.0f));
	tPrismRitem->ObjCBIndex = objCBIndex++;
	tPrismRitem->Geo = mGeometries["shapeGeo"].get();
	tPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	tPrismRitem->IndexCount = tPrismRitem->Geo->DrawArgs["tPrism"].IndexCount;
	tPrismRitem->StartIndexLocation = tPrismRitem->Geo->DrawArgs["tPrism"].StartIndexLocation;
	tPrismRitem->BaseVertexLocation = tPrismRitem->Geo->DrawArgs["tPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(tPrismRitem));

	auto hPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&hPrismRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-7.0f, 2.0f, 7.0f));
	hPrismRitem->ObjCBIndex = objCBIndex++;
	hPrismRitem->Geo = mGeometries["shapeGeo"].get();
	hPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	hPrismRitem->IndexCount = hPrismRitem->Geo->DrawArgs["hPrism"].IndexCount;
	hPrismRitem->StartIndexLocation = hPrismRitem->Geo->DrawArgs["hPrism"].StartIndexLocation;
	hPrismRitem->BaseVertexLocation = hPrismRitem->Geo->DrawArgs["hPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(hPrismRitem));

	auto coneRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(0.5f, 1.0f, 0.5f) * XMMatrixTranslation(-7.5f, 5.0f, 7.0f));
	coneRitem->ObjCBIndex = objCBIndex++;
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	//-----Tower-----//

	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(5.0f, 5.0f, 5.0f));
	pyramidRitem->ObjCBIndex = objCBIndex++;
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));

	auto sphereRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(5.0f, 7.0f, 5.0f));
	sphereRitem->ObjCBIndex = objCBIndex++;
	sphereRitem->Geo = mGeometries["shapeGeo"].get();
	sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem));

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

