//
// ta_structs.h - Dreamcast PowerVR2 Tile Accelerator parameter structs
//
// All structs here mirror the hardware's binary DMA layout exactly.
// #pragma pack(1) + endian-aware bitfields are required for correctness.
// DO NOT reorder fields or remove padding members.
//
#pragma once
#pragma pack(push, 1)

// ─── PCW (Parameter Control Word) ────────────────────────────────────────────
// 4 bytes. First word of every TA parameter. Determines what follows.
union PCW
{
#if HOST_ENDIAN == ENDIAN_LITTLE
    struct
    {
        // Obj Control (byte 0)
        u32 UV_16bit    : 1;
        u32 Gouraud     : 1;
        u32 Offset      : 1;
        u32 Texture     : 1;
        u32 Col_Type    : 2;
        u32 Volume      : 1;
        u32 Shadow      : 1;

        u32 Reserved    : 8;

        // Group Control (byte 2)
        u32 User_Clip   : 2;
        u32 Strip_Len   : 2;
        u32 Res_2       : 3;
        u32 Group_En    : 1;

        // Para Control (byte 3)
        u32 ListType    : 3;
        u32 Res_1       : 1;
        u32 EndOfStrip  : 1;
        u32 ParaType    : 3;
    };
    u8 obj_ctrl; // byte 0 only, used for LUT indexing
#else
    struct
    {
        // Para Control (byte 3, big-endian MSB first)
        u32 ParaType    : 3;
        u32 EndOfStrip  : 1;
        u32 Res_1       : 1;
        u32 ListType    : 3;

        // Group Control (byte 2)
        u32 Group_En    : 1;
        u32 Res_2       : 3;
        u32 Strip_Len   : 2;
        u32 User_Clip   : 2;

        u32 Reserved    : 8;

        // Obj Control (byte 0)
        u32 Shadow      : 1;
        u32 Volume      : 1;
        u32 Col_Type    : 2;
        u32 Texture     : 1;
        u32 Offset      : 1;
        u32 Gouraud     : 1;
        u32 UV_16bit    : 1;
    };
    struct
    {
        u32 obj_pad  : 24;
        u8  obj_ctrl : 8;
    };
#endif
    u32 full;
};


// ─── ISP/TSP Instruction Word ─────────────────────────────────────────────────
union ISP_TSP
{
#if HOST_ENDIAN == ENDIAN_LITTLE
    struct
    {
        u32 Reserved    : 20;
        u32 DCalcCtrl   : 1;
        u32 CacheBypass : 1;
        u32 UV_16b      : 1;   // Overridden by PCW in TA path
        u32 Gouraud     : 1;   // Overridden by PCW in TA path
        u32 Offset      : 1;   // Overridden by PCW in TA path
        u32 Texture     : 1;   // Overridden by PCW in TA path
        u32 ZWriteDis   : 1;
        u32 CullMode    : 2;
        u32 DepthMode   : 3;
    };
#else
    struct
    {
        u32 DepthMode   : 3;
        u32 CullMode    : 2;
        u32 ZWriteDis   : 1;
        u32 Texture     : 1;
        u32 Offset      : 1;
        u32 Gouraud     : 1;
        u32 UV_16b      : 1;
        u32 CacheBypass : 1;
        u32 DCalcCtrl   : 1;
        u32 Reserved    : 20;
    };
#endif
    u32 full;
};


// ─── TSP Instruction Word ─────────────────────────────────────────────────────
union TSP
{
    struct
    {
#if HOST_ENDIAN == ENDIAN_LITTLE
        u32 TexV        : 3;
        u32 TexU        : 3;
        u32 ShadInstr   : 2;
        u32 MipMapD     : 4;
        u32 SupSample   : 1;
        u32 FilterMode  : 2;
        u32 ClampV      : 1;
        u32 ClampU      : 1;
        u32 FlipV       : 1;
        u32 FlipU       : 1;
        u32 IgnoreTexA  : 1;
        u32 UseAlpha    : 1;
        u32 ColorClamp  : 1;
        u32 FogCtrl     : 2;
        u32 DstSelect   : 1; // Secondary Accumulator
        u32 SrcSelect   : 1; // Primary Accumulator
        u32 DstInstr    : 3;
        u32 SrcInstr    : 3;
#else
        u32 SrcInstr    : 3;
        u32 DstInstr    : 3;
        u32 SrcSelect   : 1;
        u32 DstSelect   : 1;
        u32 FogCtrl     : 2;
        u32 ColorClamp  : 1;
        u32 UseAlpha    : 1;
        u32 IgnoreTexA  : 1;
        u32 FlipU       : 1;
        u32 FlipV       : 1;
        u32 ClampU      : 1;
        u32 ClampV      : 1;
        u32 FilterMode  : 2;
        u32 SupSample   : 1;
        u32 MipMapD     : 4;
        u32 ShadInstr   : 2;
        u32 TexU        : 3;
        u32 TexV        : 3;
#endif
    };
    u32 full;
};


// ─── Texture Control Word ─────────────────────────────────────────────────────
union TCW
{
    struct  // Non-paletted textures
    {
#if HOST_ENDIAN == ENDIAN_LITTLE
        u32 TexAddr     : 21;
        u32 Reserved    : 4;
        u32 StrideSel   : 1;
        u32 ScanOrder   : 1;
        u32 PixelFmt    : 3;
        u32 VQ_Comp     : 1;
        u32 MipMapped   : 1;
#else
        u32 MipMapped   : 1;
        u32 VQ_Comp     : 1;
        u32 PixelFmt    : 3;
        u32 ScanOrder   : 1;
        u32 StrideSel   : 1;
        u32 Reserved    : 4;
        u32 TexAddr     : 21;
#endif
    } NO_PAL;
    struct  // Paletted textures (PalSelect replaces Reserved+StrideSel+ScanOrder)
    {
#if HOST_ENDIAN == ENDIAN_LITTLE
        u32 TexAddr     : 21;
        u32 PalSelect   : 6;
        u32 PixelFmt    : 3;
        u32 VQ_Comp     : 1;
        u32 MipMapped   : 1;
#else
        u32 MipMapped   : 1;
        u32 VQ_Comp     : 1;
        u32 PixelFmt    : 3;
        u32 PalSelect   : 6;
        u32 TexAddr     : 21;
#endif
    } PAL;
    u32 full;
};


// ─── Raw 32-byte TA DMA Entry ─────────────────────────────────────────────────
struct Ta_Dma
{
    PCW pcw;            // offset 0: Parameter Control Word
    union
    {
        u8  data_8[32 - 4];
        u32 data_32[8 - 1];
    };
};


// ─── Polygon Parameter Types ──────────────────────────────────────────────────


/*
// Type 0: Packed/Floating Color (32 bytes)
Polygon Type 0(Packed/Floating Color)	
0x00	Parameter Control Word		
0x04	ISP/TSP Instruction Word		
0x08	TSP Instruction Word		
0x0C	Texture Control Word		
0x10	(ignored)		
0x14	(ignored)		
0x18	Data Size for Sort DMA		
0x1C	Next Address for Sort DMA		
*/
//32B
struct TA_PolyParam0
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;
    TCW     tcw;
    u32     ign1;
    u32     ign2;
    u32     SDMA_SIZE; // Sort DMA
    u32     SDMA_ADDR;
};

// Type 1: Intensity, no Offset Color (32 bytes)
/*
Polygon Type 1(Intensity, no Offset Color)
0x00	Parameter Control Word
0x04	ISP/TSP Instruction Word
0x08	TSP Instruction Word
0x0C	Texture Control Word
0x10	Face Color Alpha
0x14	Face Color R
0x18	Face Color G
0x1C	Face Color B
*/
//32B
struct TA_PolyParam1
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;
    TCW     tcw;
    f32     FaceColorA;
    f32     FaceColorR;
    f32     FaceColorG;
    f32     FaceColorB;
};

/*
// Type 2A: Intensity with Offset Color, first 32 bytes
0x00	Parameter Control Word		
0x04	ISP/TSP Instruction Word		
0x08	TSP Instruction Word		
0x0C	Texture Control Word		
0x10	(ignored)			
0x14	(ignored)			
0x18	Data Size for Sort DMA		
0x1C	Next Address for Sort DMA
0x20	Face Color Alpha			
0x24	Face Color R			
0x28	Face Color G			
0x2C	Face Color B			
0x30	Face Offset Color Alpha			
0x34	Face Offset Color R			
0x38	Face Offset Color G			
0x3C	Face Offset Color B			
*/
struct TA_PolyParam2A
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;
    TCW     tcw;
    u32     ign1;
    u32     ign2;
    u32     SDMA_SIZE;
    u32     SDMA_ADDR;
};

// Type 2B: Intensity with Offset Color, second 32 bytes
struct TA_PolyParam2B
{
    f32 FaceColorA,  FaceColorR,  FaceColorG,  FaceColorB;
    f32 FaceOffsetA, FaceOffsetR, FaceOffsetG, FaceOffsetB;
};
/*
// Type 3: Packed Color, Two Volumes (32 bytes)
0x00	Parameter Control Word
0x04	ISP/TSP Instruction Word
0x08	TSP Instruction Word 0
0x0C	Texture Control Word 0
0x10	TSP Instruction Word 1
0x14	Texture Control Word 1
0x18	Data Size for Sort DMA
0x1C	Next Address for Sort DMA
*/
struct TA_PolyParam3
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;   // Volume 0
    TCW     tcw;
    TSP     tsp1;  // Volume 1
    TCW     tcw1;
    u32     SDMA_SIZE;
    u32     SDMA_ADDR;
};

/*
// Type 4A: Intensity, Two Volumes, first 32 bytes
0x00	Parameter Control Word
0x04	ISP/TSP Instruction Word
0x08	TSP Instruction Word 0
0x0C	Texture Control Word 0
0x10	TSP Instruction Word 1
0x14	Texture Control Word 1
0x18	Data Size for Sort DMA
0x1C	Next Address for Sort DMA
0x20	Face Color Alpha 0
0x24	Face Color R 0
0x28	Face Color G 0
0x2C	Face Color B 0
0x30	Face Color Alpha 1
0x34	Face Color R 1
0x38	Face Color G 1
0x3C	Face Color B 1
*/
struct TA_PolyParam4A
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;
    TCW     tcw;
    TSP     tsp1;
    TCW     tcw1;
    u32     SDMA_SIZE;
    u32     SDMA_ADDR;
};

// Type 4B: Intensity, Two Volumes, second 32 bytes
struct TA_PolyParam4B
{
    f32 FaceColor0A, FaceColor0R, FaceColor0G, FaceColor0B;
    f32 FaceColor1A, FaceColor1R, FaceColor1G, FaceColor1B;
};


// ─── Modifier Volume & Sprite Params ─────────────────────────────────────────

struct TA_ModVolParam
{
    PCW     pcw;
    ISP_TSP isp;
    u32     ign[8 - 2];
};

struct TA_SpriteParam
{
    PCW     pcw;
    ISP_TSP isp;
    TSP     tsp;
    TCW     tcw;
    u32     BaseCol;
    u32     OffsCol;
    u32     SDMA_SIZE;
    u32     SDMA_ADDR;
};


// ─── UV 16-bit helper macro ───────────────────────────────────────────────────
// On little-endian, u and v are swapped in memory to match DC layout
#if HOST_ENDIAN == ENDIAN_LITTLE
    #define UV_U16(u, v) u16 v, u;
#else
    #define UV_U16(u, v) u16 u, v;
#endif


// ─── Vertex Parameter Types ───────────────────────────────────────────────────
// All A-structs are 28 bytes (7 words), B-structs are 32 bytes (8 words).
// They are always preceded by a 4-byte PCW, so A fits in one 32-byte DMA block.

struct TA_Vertex0  { f32 xyz[3]; u32 ign1, ign2; u32 BaseCol; u32 ign3; };                         // Non-Textured, Packed Color
struct TA_Vertex1  { f32 xyz[3]; f32 BaseA, BaseR, BaseG, BaseB; };                                 // Non-Textured, Floating Color
struct TA_Vertex2  { f32 xyz[3]; u32 ign1, ign2; f32 BaseInt; u32 ign3; };                          // Non-Textured, Intensity
struct TA_Vertex3  { f32 xyz[3]; f32 u, v; u32 BaseCol; u32 OffsCol; };                             // Textured, Packed Color
struct TA_Vertex4  { f32 xyz[3]; UV_U16(u, v) u32 ign1; u32 BaseCol; u32 OffsCol; };               // Textured, Packed Color, 16-bit UV
struct TA_Vertex5A { f32 xyz[3]; f32 u, v; u32 ign1, ign2; };                                       // Textured, Floating Color (A)
struct TA_Vertex5B { f32 BaseA, BaseR, BaseG, BaseB; f32 OffsA, OffsR, OffsG, OffsB; };            // Textured, Floating Color (B)
struct TA_Vertex6A { f32 xyz[3]; UV_U16(u, v) u32 ign1, ign2, ign3; };                              // Textured, Floating Color, 16-bit UV (A)
struct TA_Vertex6B { f32 BaseA, BaseR, BaseG, BaseB; f32 OffsA, OffsR, OffsG, OffsB; };            // Textured, Floating Color, 16-bit UV (B)
struct TA_Vertex7  { f32 xyz[3]; f32 u, v; f32 BaseInt; f32 OffsInt; };                             // Textured, Intensity
struct TA_Vertex8  { f32 xyz[3]; UV_U16(u, v) u32 ign1; f32 BaseInt; f32 OffsInt; };               // Textured, Intensity, 16-bit UV
struct TA_Vertex9  { f32 xyz[3]; u32 BaseCol0; u32 BaseCol1; u32 ign1, ign2; };                     // Non-Textured, Packed Color, Two Volumes
struct TA_Vertex10 { f32 xyz[3]; f32 BaseInt0; f32 BaseInt1; u32 ign1, ign2; };                     // Non-Textured, Intensity, Two Volumes

struct TA_Vertex11A { f32 xyz[3]; f32 u0, v0; u32 BaseCol0, OffsCol0; };                            // Textured, Packed Color, Two Volumes (A)
struct TA_Vertex11B { f32 u1, v1; u32 BaseCol1, OffsCol1; u32 ign1, ign2, ign3, ign4; };           // Textured, Packed Color, Two Volumes (B)

struct TA_Vertex12A { f32 xyz[3]; UV_U16(u0, v0) u32 ign1; u32 BaseCol0, OffsCol0; };              // Textured, Packed Color, 16-bit UV, Two Volumes (A)
struct TA_Vertex12B { UV_U16(u1, v1) u32 ign2; u32 BaseCol1, OffsCol1; u32 ign3, ign4, ign5, ign6; }; // (B)

struct TA_Vertex13A { f32 xyz[3]; f32 u0, v0; f32 BaseInt0, OffsInt0; };                            // Textured, Intensity, Two Volumes (A)
struct TA_Vertex13B { f32 u1, v1; f32 BaseInt1, OffsInt1; u32 ign1, ign2, ign3, ign4; };           // (B)

struct TA_Vertex14A { f32 xyz[3]; UV_U16(u0, v0) u32 ign1; f32 BaseInt0, OffsInt0; };              // Textured, Intensity, 16-bit UV, Two Volumes (A)
struct TA_Vertex14B { UV_U16(u1, v1) u32 ign2; f32 BaseInt1, OffsInt1; u32 ign3, ign4, ign5, ign6; }; // (B)


// ─── Sprite Vertex Types ──────────────────────────────────────────────────────

struct TA_Sprite0A { f32 x0, y0, z0; f32 x1, y1, z1; f32 x2; };
struct TA_Sprite0B { f32 y2, z2; f32 x3, y3; u32 ign1, ign2, ign3, ign4; };

struct TA_Sprite1A { f32 x0, y0, z0; f32 x1, y1, z1; f32 x2; };
struct TA_Sprite1B { f32 y2, z2; f32 x3, y3; u32 ign1; UV_U16(u0, v0) UV_U16(u1, v1) UV_U16(u2, v2) };


// ─── Modifier Volume Vertex ───────────────────────────────────────────────────

struct TA_ModVolA { PCW pcw; f32 x0, y0, z0; f32 x1, y1, z1; f32 x2; };
struct TA_ModVolB { f32 y2, z2; u32 ign[6]; };


// ─── Combined Vertex Parameter (union over all types) ─────────────────────────

struct TA_VertexParam
{
    union
    {
        struct
        {
            PCW pcw;
            union
            {
                u8          Raw[64 - 4];

                TA_Vertex0  vtx0;
                TA_Vertex1  vtx1;
                TA_Vertex2  vtx2;
                TA_Vertex3  vtx3;
                TA_Vertex4  vtx4;

                struct { TA_Vertex5A vtx5A; TA_Vertex5B vtx5B; };
                struct { TA_Vertex6A vtx6A; TA_Vertex6B vtx6B; };

                TA_Vertex7  vtx7;
                TA_Vertex8  vtx8;
                TA_Vertex9  vtx9;
                TA_Vertex10 vtx10;

                struct { TA_Vertex11A vtx11A; TA_Vertex11B vtx11B; };
                struct { TA_Vertex12A vtx12A; TA_Vertex12B vtx12B; };
                struct { TA_Vertex13A vtx13A; TA_Vertex13B vtx13B; };
                struct { TA_Vertex14A vtx14A; TA_Vertex14B vtx14B; };

                struct { TA_Sprite0A spr0A; TA_Sprite0B spr0B; };
                struct { TA_Sprite1A spr1A; TA_Sprite1B spr1B; };
            };
        };
        struct { TA_ModVolA mvolA; TA_ModVolB mvolB; };
    };
};

#pragma pack(pop)
