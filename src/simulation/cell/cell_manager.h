#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <cstddef> // for offsetof

#include "../../rendering/core/shader_class.h"
#include "../../input/input.h"
#include "../../core/config.h"
#include "../../rendering/core/mesh/sphere_mesh.h"
#include "../genome/genome.h"
#include "../../rendering/systems/frustum_culling.h"

// Forward declaration
class Camera;

// GPU compute cell structure matching the compute shader
struct ComputeCell {
    // Physics:
    glm::vec4 positionAndMass{0, 0, 0, 1};       // x, y, z, mass
    glm::vec4 velocity{};
    glm::vec4 acceleration{};
    glm::quat orientation{1., 0., 0., 0.};  // angular stuff in quaternions to prevent gimbal lock
    glm::quat angularVelocity{ 1., 0., 0., 0. };
    glm::quat angularAcceleration{ 1., 0., 0., 0. };

    // Internal:
    glm::vec4 signallingSubstances{}; // 4 substances for now
    int modeIndex{ 0 };
    float age{ 0 };                      // also used for split timer
    float toxins{ 0 };
    float nitrates{ 1 };
    
    // Unique ID system: X.Y.Z format
    // X = parent ID (32 bits), Y = cell ID (31 bits), Z = child flag (1 bit, 0=A, 1=B)
    uint64_t uniqueID{ 0 };              // Packed ID: [parent(32)] [cell(31)] [child(1)]
    uint64_t justSplit{ 0 };              // Use this as the justSplit flag
    uint32_t padding2[4]{ 0, 0, 0, 0 };  // Additional padding to ensure 16-byte alignment
    
    float getRadius() const
    {
        return static_cast<float>(pow(positionAndMass.w, 1.0f/3.0f));
    }
    
    // ID utility functions
    uint32_t getParentID() const { return static_cast<uint32_t>((uniqueID >> 32) & 0xFFFFFFFF); }
    uint32_t getCellID() const { return static_cast<uint32_t>((uniqueID >> 1) & 0x7FFFFFFF); }
    uint8_t getChildFlag() const { return static_cast<uint8_t>(uniqueID & 0x1); }
    
    void setUniqueID(uint32_t parentID, uint32_t cellID, uint8_t childFlag) {
        uniqueID = (static_cast<uint64_t>(parentID) << 32) | 
                   (static_cast<uint64_t>(cellID & 0x7FFFFFFF) << 1) | 
                   (childFlag & 0x1);
    }
};

// Ensure struct alignment is correct for GPU usage
static_assert(sizeof(ComputeCell) % 16 == 0, "ComputeCell must be 16-byte aligned for GPU usage");
static_assert(offsetof(ComputeCell, uniqueID) % 8 == 0, "uniqueID must be 8-byte aligned");

struct CellManager
{
    // GPU-based cell management using compute shaders
    // This replaces the CPU-based vectors with GPU buffer objects
    // The compute shaders handle physics calculations and position updates

    // GPU buffer objects - Double buffered for performance
    GLuint cellBuffer[3]{};         // SSBO for compute cell data (double buffered)
    GLuint instanceBuffer{};        // VBO for instance rendering data
    int bufferRotation{};

    // Cell count management
    GLuint gpuCellCountBuffer{};     // GPU-accessible cell count buffer
    GLuint stagingCellCountBuffer{}; // CPU-accessible cell count buffer (no sync stalls)
    GLuint cellAdditionBuffer{};     // Cell addition queue for GPU

    // Cell data staging buffer for CPU reads (avoids GPU->CPU transfer warnings)
    GLuint stagingCellBuffer{};      // CPU-accessible cell data buffer
    void* mappedCellPtr = nullptr;   // Pointer to the cell data staging buffer

    // Genome buffer (immutable, no need for double buffering)
    // It might be a good idea in the future to switch from a flattened mode array to genome structs that contain their own mode arrays
    GLuint modeBuffer{};

    // Unique ID management buffers
    GLuint idPoolBuffer{};        // SSBO for available cell IDs (queue-like structure)
    GLuint idCounterBuffer{};     // SSBO for ID counters (next available ID, pool size)
    GLuint idRecycleBuffer{};     // SSBO for recycled IDs from dead cells

    // Spatial partitioning buffers - Double buffered
    GLuint gridBuffer{};       // SSBO for grid cell data (stores cell indices)
    GLuint gridCountBuffer{};  // SSBO for grid cell counts
    GLuint gridOffsetBuffer{}; // SSBO for grid cell starting offsets
    
    // PERFORMANCE OPTIMIZATION: Additional buffers for 100k cells
    GLuint gridHashBuffer{};   // Hash-based lookup for sparse grids
    GLuint activeCellsBuffer{}; // Buffer containing only active grid cells
    uint32_t activeGridCount{0}; // Number of active grid cells

    // Sphere mesh for instanced rendering
    SphereMesh sphereMesh;

    // LOD system shaders
    Shader* lodComputeShader = nullptr;       // Compute shader for LOD calculation
    Shader* lodVertexShader = nullptr;        // LOD vertex/fragment shader
    
    // LOD configuration
    float lodDistances[4] = {20.0f, 60.0f, 150.0f, 250.0f}; // Distance thresholds for LOD levels
    bool useLODSystem = true;                 // Enable/disable LOD system
    
    // LOD instance buffers - separate buffer for each LOD level
    GLuint lodInstanceBuffers[4]{};           // Instance buffers for each LOD level
    GLuint lodCountBuffer{};                  // Buffer to track instance counts per LOD level
    int lodInstanceCounts[4]{};               // CPU-side copy of LOD instance counts
    
    // Frustum culling system
    Shader* frustumCullShader = nullptr;      // Compute shader for frustum culling
    Shader* frustumCullLODShader = nullptr;   // Compute shader for frustum culling with LOD
    GLuint visibleInstanceBuffer{};           // Buffer for frustum-culled instances
    GLuint visibleCountBuffer{};              // Buffer for visible instance count
    bool useFrustumCulling = true;            // Enable/disable frustum culling
    Frustum currentFrustum;                   // Current camera frustum
    int visibleCellCount{0};                  // Number of visible cells after culling
    
    // LOD statistics functions
    int getTotalTriangleCount() const;        // Calculate total triangles across all LOD levels
    int getTotalVertexCount() const;          // Calculate total vertices across all LOD levels

    // Asynchronous readback system for performance monitoring
    //GLuint readbackBuffer = 0;      // Buffer for async GPU->CPU data transfer
    //GLsync readbackFence = nullptr; // Sync object for async operations
    //bool readbackInProgress = false;
    //float readbackCooldown = 0.0f; // Timer to limit readback frequency

    // Compute shaders
    Shader* physicsShader = nullptr;
    Shader* updateShader = nullptr;
    Shader* extractShader = nullptr; // For extracting instance data efficiently
    Shader* internalUpdateShader = nullptr;
    Shader* cellCounterShader = nullptr;
	Shader* cellAdditionShader = nullptr;
    Shader* idManagerShader = nullptr;  // For managing unique IDs

    // Spatial partitioning compute shaders
    Shader* gridClearShader = nullptr;     // Clear grid counts
    Shader* gridAssignShader = nullptr;    // Assign cells to grid
    Shader* gridPrefixSumShader = nullptr; // Calculate grid offsets
    Shader* gridInsertShader = nullptr;    // Insert cells into grid
    
    // CPU-side storage for initialization and debugging
    // Note: cpuCells is deprecated in favor of GPU buffers, should be removed after refactoring
    std::vector<ComputeCell> cpuCells;
    std::vector<ComputeCell> cellStagingBuffer;
    
    // Cell count tracking (CPU-side approximation of GPU state)
    int cellCount{0};               // Approximate cell count, may not reflect exact GPU state due to being a frame behind
    int cpuPendingCellCount{0};     // Number of cells pending addition by CPU
    int gpuPendingCellCount{0};     // Approx number of cells pending addition by GPU
	// Mysteriously the value read on cpu is always undershooting significantly so you're better off treating it as a bool than an int
    void* mappedPtr = nullptr;      // Pointer to the cell count staging buffer
    GLuint* countPtr = nullptr;     // Typed pointer to the mapped buffer value

    // Configuration
    static constexpr int MAX_CELLS = config::MAX_CELLS;
    static constexpr int DEFAULT_CELL_COUNT = config::DEFAULT_CELL_COUNT;
    float spawnRadius = config::DEFAULT_SPAWN_RADIUS;
    int cellLimit = config::MAX_CELLS;

    // Constructor and destructor
    CellManager();
    ~CellManager();

    // We declare functions in the struct, but we will define them in the cell_manager.cpp file.
    // This is because when a file is edited, the compiler will also have to recompile all the files that include it.
    // So we will define the functions in a separate file to avoid recompiling the whole project when we change the implementation.

    void initializeGPUBuffers();
    void resetSimulation();
    void spawnCells(int count = DEFAULT_CELL_COUNT);
    void renderCells(glm::vec2 resolution, Shader &cellShader, class Camera &camera, bool wireframe = false);
    // Gizmo orientation visualization
    GLuint gizmoBuffer{};           // Buffer for gizmo line vertices
    GLuint gizmoVAO{};              // VAO for gizmo rendering
    GLuint gizmoVBO{};              // VBO for gizmo vertices
    Shader* gizmoExtractShader = nullptr; // Compute shader for generating gizmo data
    Shader* gizmoShader = nullptr;        // Vertex/fragment shaders for rendering gizmos
    
    // Ring gizmo visualization
    GLuint ringGizmoBuffer{};           // Buffer for ring gizmo vertices
    GLuint ringGizmoVAO{};              // VAO for ring gizmo rendering
    GLuint ringGizmoVBO{};              // VBO for ring gizmo vertices
    Shader* ringGizmoExtractShader = nullptr; // Compute shader for generating ring gizmo data
    Shader* ringGizmoShader = nullptr;        // Vertex/fragment shaders for rendering ring gizmos
    
    // Adhesion line visualization
    GLuint adhesionLineBuffer{};        // Buffer for adhesion line vertices  
    GLuint adhesionLineVAO{};           // VAO for adhesion line rendering
    GLuint adhesionLineVBO{};           // VBO for adhesion line vertices

    Shader* adhesionLineShader = nullptr;        // Vertex/fragment shaders for rendering adhesion lines
    

    
    // Adhesion connection system - permanent connections between sibling cells
    GLuint adhesionConnectionBuffer{};  // Buffer storing permanent adhesion connections
    Shader* adhesionConnectionShader = nullptr;  // Compute shader for establishing initial connections
    int adhesionConnectionCount{0};
    
    // Optimized adhesion line system with spatial indexing
    GLuint adhesionParentIndexBuffer{};  // Buffer for spatial index (parent lookup table)
    GLuint adhesionParentIndexCounterBuffer{};  // Counter buffer for parent index building
    GLuint adhesionOptimizedCountBuffer{};  // Count buffer for optimized shader (cellCount, parentIndexCount)
    Shader* adhesionParentIndexBuilderShader = nullptr;  // Compute shader for building spatial index
    Shader* adhesionLineOptimizedShader = nullptr;  // Optimized adhesion line extract shader
    int adhesionParentIndexCount{0};  // Number of parent indices
    bool adhesionIndexNeedsUpdate{true};  // Flag to track when index needs rebuilding
    
    // Rendering optimization flags
    bool useSpatialIndexing{true};  // Use spatial indexing for O(1) sibling lookup

    void initializeGizmoBuffers();
    void updateGizmoData();
    void cleanupGizmos();
    void renderGizmos(glm::vec2 resolution, const Camera& camera, bool showGizmos);
    
    // Ring gizmo methods
    void renderRingGizmos(glm::vec2 resolution, const class Camera &camera, const class UIManager &uiManager);
    void initializeRingGizmoBuffers();
    void updateRingGizmoData();
    void cleanupRingGizmos();
    
    // Adhesion line methods
    void renderAdhesionLines(glm::vec2 resolution, const class Camera &camera, bool showAdhesionLines);
    void initializeAdhesionLineBuffers();
    void cleanupAdhesionLines();
    
    // Optimized adhesion line methods (spatial indexing)
    void renderOptimizedAdhesionLines(glm::vec2 resolution, const class Camera &camera, bool showAdhesionLines);
    
    // Adhesion connection methods
    void initializeAdhesionConnectionSystem();
    void establishAdhesionConnections();
    void cleanupAdhesionConnectionSystem();
    
    // Optimized adhesion line methods with spatial indexing
    void initializeOptimizedAdhesionLineSystem();
    void updateSpatialIndexAdhesionLineData();
    void renderOptimizedAdhesionLinesWithIndexing(glm::vec2 resolution, const class Camera &camera, bool showAdhesionLines);
    void cleanupOptimizedAdhesionLineSystem();

    void addCellsToGPUBuffer(const std::vector<ComputeCell> &cells);
    void addCellToGPUBuffer(const ComputeCell &newCell);
    void addCellToStagingBuffer(const ComputeCell &newCell);
    void addCell(const ComputeCell &newCell) { addCellToStagingBuffer(newCell); }
    void addStagedCellsToGPUBuffer();
    void addGenomeToBuffer(GenomeData& genomeData) const;
    void updateCells(float deltaTime);
    void cleanup();

    // Spatial partitioning functions
    void initializeSpatialGrid();
    void updateSpatialGrid();
    void cleanupSpatialGrid();

    // ID management functions
    void initializeIDSystem();
    void cleanupIDSystem();
    void recycleDeadCellIDs(); // Called when cells die to recycle their IDs
    void printCellIDs(int maxCells = 10); // Debug function to print cell IDs

    // Getter functions for debug information
    int getCellCount() const { return cellCount; }
    float getSpawnRadius() const { return spawnRadius; }

    // GPU pipeline status getters
    //bool isReadbackInProgress() const { return readbackInProgress; }
    //bool isReadbackSystemHealthy() const { return readbackBuffer != 0; }
    //float getReadbackCooldown() const { return readbackCooldown; }

    // Performance testing function
	// Cell selection and interaction system
    struct SelectedCellInfo
    {
        int cellIndex = -1;
        ComputeCell cellData;
        bool isValid = false;
        glm::vec3 dragOffset = glm::vec3(0.0f); // Offset from cell center when dragging starts
        float dragDistance = 10.0f;             // Distance from camera to maintain during dragging
    };

    SelectedCellInfo selectedCell;
    bool isDraggingCell = false;

    // Selection and interaction functions
    void handleMouseInput(const glm::vec2 &mousePos, const glm::vec2 &screenSize,
                          const class Camera &camera, bool isMousePressed, bool isMouseDown,
                          float scrollDelta = 0.0f);
    int selectCellAtPosition(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection);
    void dragSelectedCell(const glm::vec3 &newWorldPosition);
    void clearSelection();

    // Handle the end of dragging (restore physics)
    void endDrag();

    // GPU synchronization for selection (synchronous readback for immediate use)
    void syncCellPositionsFromGPU();

    // Utility functions for mouse interaction
    glm::vec3 calculateMouseRay(const glm::vec2 &mousePos, const glm::vec2 &screenSize,
                                const class Camera &camera);
    bool raySphereIntersection(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection,
                               const glm::vec3 &sphereCenter, float sphereRadius, float &distance);

    // Getters for selection system
    bool hasSelectedCell() const { return selectedCell.isValid; }
    const SelectedCellInfo &getSelectedCell() const { return selectedCell; }
    ComputeCell getCellData(int index) const;
    void updateCellData(int index, const ComputeCell &newData); // Needs refactoring

    // Memory barrier optimization system
    // Forward declaration and performance monitoring
    struct BarrierStats {
        int totalBarriers = 0;
        int batchedBarriers = 0;
        int flushCalls = 0;
        float barrierEfficiency = 0.0f; // batchedBarriers / totalBarriers
        
        void reset() {
            totalBarriers = 0;
            batchedBarriers = 0;
            flushCalls = 0;
            barrierEfficiency = 0.0f;
        }
        
        void updateEfficiency() {
            if (totalBarriers > 0) {
                barrierEfficiency = static_cast<float>(batchedBarriers) / totalBarriers;
            }
        }
    };
    
    struct BarrierBatch {
        GLbitfield pendingBarriers = 0;
        bool needsFlush = false;
        mutable BarrierStats* stats = nullptr; // Reference to stats for tracking
        
        void addBarrier(GLbitfield barrier) {
            pendingBarriers |= barrier;
            if (stats) {
                stats->totalBarriers++;
                if (pendingBarriers != barrier) {
                    stats->batchedBarriers++; // This barrier was batched with others
                }
            }
        }
        
        void flush() {
            if (pendingBarriers != 0) {
                glMemoryBarrier(pendingBarriers);
                pendingBarriers = 0;
                if (stats) {
                    stats->flushCalls++;
                    stats->updateEfficiency();
                }
            }
            needsFlush = false;
        }
        
        void clear() {
            pendingBarriers = 0;
            needsFlush = false;
        }
        
        void setStats(BarrierStats* statsPtr) {
            stats = statsPtr;
        }
    };
    
    mutable BarrierBatch barrierBatch;
    mutable BarrierStats barrierStats;
    
    // Optimized barrier methods
    void addBarrier(GLbitfield barrier) const { barrierBatch.addBarrier(barrier); }
    void flushBarriers() const { barrierBatch.flush(); }
    void clearBarriers() const { barrierBatch.clear(); }
    
    // Debug methods for barrier optimization
    const BarrierStats& getBarrierStats() const { return barrierStats; }
    void resetBarrierStats() const { barrierStats.reset(); }

    // Asynchronous readback functions for performance monitoring // not actually implemented yet, maybe later if we need to
    //void initializeReadbackSystem();
    //void updateReadbackSystem(float deltaTime);
    //void requestAsyncReadback();
    //bool checkAsyncReadback(ComputeCell *outputData, int maxCells);
    //void cleanupReadbackSystem();

    // Double buffering management functions
    int getRotatedIndex(int index, int max) const { return (index + bufferRotation) % max; }
    void rotateBuffers() { bufferRotation = getRotatedIndex(1, 3); }
    GLuint getCellReadBuffer() const { return cellBuffer[getRotatedIndex(0, 3)]; }
    GLuint getCellWriteBuffer() const { return cellBuffer[getRotatedIndex(1, 3)]; }

    void setCellLimit(int limit) { cellLimit = limit; }
    int getCellLimit() const { return cellLimit; }
    
    // LOD system functions
    void initializeLODSystem();
    void cleanupLODSystem();
    void updateLODLevels(const Camera& camera);
    void renderCellsLOD(glm::vec2 resolution, const Camera& camera, bool wireframe = false);
    void runLODCompute(const Camera& camera);
    
    // Frustum culling functions
    void initializeFrustumCulling();
    void cleanupFrustumCulling();
    void updateFrustum(const Camera& camera, float fov, float aspectRatio, float nearPlane, float farPlane);
    void runFrustumCulling();
    void runFrustumCullingLOD(const Camera& camera);
    int getVisibleCellCount() const { return visibleCellCount; }

    void restoreCellsDirectlyToGPUBuffer(const std::vector<ComputeCell> &cells); // For keyframe restoration
    void setCPUCellData(const std::vector<ComputeCell> &cells); // For keyframe restoration

    Shader* clearJustSplitShader = nullptr; // Compute shader to clear justSplit flag

private:
    void runPhysicsCompute(float deltaTime);
    void runUpdateCompute(float deltaTime);
    void runInternalUpdateCompute(float deltaTime);
    void runCellCounter();
    void applyCellAdditions();

    // Spatial grid helper functions
    void runGridClear();
    void runGridAssign();
    void runGridPrefixSum();
    void runGridInsert();
    
    // ID management helper functions
    void runIDManager();
};
