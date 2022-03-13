#pragma once

#include "src/Utility/DXSample.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Simulation1 : public DXSample
{
public:
  Simulation1(UINT width, UINT height, std::wstring name);

  virtual void OnInit();
  virtual void OnUpdate();
  virtual void OnRender();
  virtual void OnDestroy();

  virtual void OnKeyDown(UINT8 /*key*/);

private:
  //#######################       Constants        ###############################

  //-------Simulation Output Influencing Constants-----------
  //the value every direction of every lattice point within the starting cube is set to
  static const UINT SimulationStartValue = 1000;
  
  //how much gravity be factored in
  static const float GravityFactor;
  //how much the momentum of the particles should be factored in
  static const float MomentumExponent;
  //include terms that more precisely model realistic fluid flow
  static const bool IncludeRealityIncreasingTerms = false;

  static const float TimeStep;

  //--------Simulation Technical Constants-------------------
  static const UINT LatticeWith = 3;
  static const UINT LatticeHeight = 1;
  static const UINT LatticeDepth = 3;
  static const UINT LatticePointsPerUnit = 30;

  static const UINT LatticePointDirections = 19;
  static const UINT BitPerLatticePointDirection = 16;

  //to avoid race conditions in the compute shader,
  //the data for one lattice point has to be 4-byte aligned
  static const UINT BytePerLatticePoint = 40;

  static const UINT NUM_LATTICE_POINTS = LatticeWith * LatticeHeight * LatticeDepth * LatticePointsPerUnit * LatticePointsPerUnit * LatticePointsPerUnit;

  
  static const UINT ComputeRuns = 2;
  static const UINT LatticeBufferCount = 2;

  static const UINT LatticeBufferIn = 0;
  static const UINT LatticeBufferOut = 1;

  static const UINT ComputeDescriptorsPerRun = 3;

  struct CS_Slots {
    static const UINT Constants = 0;
    static const UINT Table = 1;
  };

  struct CS_Register {
    static const UINT LatticeIn = 0;
    static const UINT LatticeOut = 0;
    static const UINT PointBuffer = 1;
    static const UINT Constants = 0;
  };

  //--------Rendering Appearance Constants-------------
  //how many points for rending the compute shader generates per lattice point
  //can be 1 or 8
  static const UINT PointsPerLattice = 8;
  
  //the sum of all particles described by one lattice point must be greater than 
  //this threshold for the point to be rendered
  static const UINT ParticleThreshold = 1500;
  
  //defines if the liquid should be desplayed as having one uniform color 
  //otherwise it will be a color gradiend depending on the number of particles at every point
  static const bool UniformColor = true;

  //defindes the number of particles, which represent the upper bound for the color gradient
  //if UniformColor is false
  static const UINT ColorUpperBound = 1900;

  static const float VisiblityPassPointOffset;
  static const XMVECTOR eye;

  //--------Rendering Technical Constants--------------
  
  //how many frames are buffered at once
  static const UINT FrameCount = 1;
  static const UINT FinalRenderTargetOffset = 3;
  
  //texture format of the G-Buffers
  static const DXGI_FORMAT TextureFormat;

  struct GP_Slots {
    static const UINT ShaderInputTable = 0;
    static const UINT TextureTable = 1;
  };

  struct GP_DescriptorOffset{
    static const UINT Matrices = 0;
    static const UINT Visibility = 1;
    static const UINT Blending = 2;
    static const UINT Deferred = 3;
    static const UINT ShadingTextures = 4;
  };

  struct DeferredShadingTexturesIndex {
    static const UINT POS = 0;
    static const UINT NORMAL = 1;
    static const UINT COLOR = 2;
  };
  
  static const float DeferredShadingTexturesClearValues[3][4];

  //#######################       Structures        ###############################

  struct Vertex {
    XMFLOAT3 position;
    XMFLOAT2 tc;
    XMFLOAT3 normal;
  };

  struct ComputeShaderInput{
    UINT BitPerLatticePointDirection;
    UINT BytePerLatticePoint;
    UINT global_X;
    UINT global_Y;
    UINT global_Z;
    UINT particle_threshold;
    float unit_lenght;
    UINT run;
    UINT PointsPerLattice;
    float timestep;
    float gravity_factor;
    float momentum_exponent;
    UINT include_reality_increasing_terms;
  };

  struct MatrixShaderInput{
    XMMATRIX world_ground;
    XMMATRIX world_liquid;
    XMMATRIX vp;
  };

  struct VisibilityShaderInput {
    XMFLOAT3 eye;
    float radius;
    float offset;
  };

  struct BlendingShaderInput {
    XMMATRIX invers_vw;
    float radius;
    float epsilon;
    UINT particle_threshold;
    UINT color_upper_bound;
  };

  struct DeferredShadingInput {
    XMFLOAT4 eye;
    XMFLOAT4 light;
  };

  struct IndirectCommand {
    D3D12_DRAW_ARGUMENTS drawArguments;
  };

  struct VertexBuffer{
    ComPtr<ID3D12Resource> buffer;
    D3D12_VERTEX_BUFFER_VIEW bufferView;
  };


  //#######################       Variables        ###############################

  //------------- general variables --------------------
  CD3DX12_VIEWPORT m_viewport;
  CD3DX12_RECT m_scissorRect;
  ComPtr<ID3D12Device> m_device;
  UINT m_rtvDescriptorSize;
  UINT m_cbvSrvUavDescriptorSize;
  bool freeze;


  //--------------- compute resources ----------------------
  ComPtr<ID3D12RootSignature> m_computeSignature;
  ComPtr<ID3D12CommandQueue> m_computeQueue;
  ComPtr<ID3D12GraphicsCommandList> m_computeCommandList;
  ComPtr<ID3D12CommandAllocator> m_computeCommandAllocators[FrameCount];
  
  ComPtr<ID3D12DescriptorHeap> m_computeCbvSrvUavHeap;
  ComPtr<ID3D12PipelineState> m_computePSO;
  ComPtr<ID3D12Resource> m_latticeBuffer[LatticeBufferCount];


  //--------------- render resources -------------------------
  ComPtr<IDXGISwapChain3> m_swapChain;
  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12CommandQueue> m_commandQueue;
  ComPtr<ID3D12GraphicsCommandList> m_commandList;
  ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];

  ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
  ComPtr<ID3D12DescriptorHeap> m_graphicsCbvSrvUavHeap;


  ComPtr<ID3D12Resource> m_renderTargets[FrameCount > 2 ? FrameCount : 2];
  ComPtr<ID3D12Resource> m_depthBuffer;

  ComPtr<ID3D12Resource> m_indirectDrawBuffer;
  ComPtr<ID3D12Resource> m_indirectDrawBufferReset;
  ComPtr<ID3D12CommandSignature> m_commandSignature;

  ComPtr<ID3D12Resource> m_deferredShadingTextures[3];

  struct {
    ComPtr<ID3D12Resource> matrix;
    ComPtr<ID3D12Resource> visiblity;
    ComPtr<ID3D12Resource> blending;
    ComPtr<ID3D12Resource> shading;
  } m_constShaderInput;

  //pipeline states
  struct {
    struct {
      ComPtr<ID3D12PipelineState> liquid;
      ComPtr<ID3D12PipelineState> ground;
    }visibility;

    struct {
      ComPtr<ID3D12PipelineState> liquid;
      ComPtr<ID3D12PipelineState> ground;
    }blending;

    ComPtr<ID3D12PipelineState> deferred;
  } m_GPSO;
 
  struct {
    VertexBuffer ground;
    VertexBuffer finalQuad;
    VertexBuffer liquid;
  } m_vertexInput;


  // ------------- Synchronization objects ---------------
  UINT m_frameIndex;
  HANDLE m_fenceEvent;
  HANDLE m_computeFenceEvent;
  ComPtr<ID3D12Fence> m_fence;
  ComPtr<ID3D12Fence> m_computeFence;
  ComPtr<ID3D12Fence> m_computeSyncFence;
  UINT64 m_fenceValues[FrameCount];
  UINT64 m_computeSyncFenceValue;
   

  void LoadPipeline();
  void LoadAssets();
  void PopulateCommandList();
  void MoveToNextFrame();
  void WaitForGpu();

  UINT* generateInitialLatticeBufferData(UINT latticeBufferSize);
};
