/*
 * GDI definitions
 *
 * Copyright 1993 Alexandre Julliard
 */

#ifndef GDI_H
#define GDI_H

#include "windows.h"
#include "ldt.h"
#include "local.h"
#include "x11drv.h"

  /* GDI objects magic numbers */
#define PEN_MAGIC             0x4f47
#define BRUSH_MAGIC           0x4f48
#define FONT_MAGIC            0x4f49
#define PALETTE_MAGIC         0x4f4a
#define BITMAP_MAGIC          0x4f4b
#define REGION_MAGIC          0x4f4c
#define DC_MAGIC              0x4f4d
#define DISABLED_DC_MAGIC     0x4f4e
#define META_DC_MAGIC         0x4f4f
#define METAFILE_MAGIC        0x4f50
#define METAFILE_DC_MAGIC     0x4f51
#define MAGIC_DONTCARE	      0xffff

typedef struct tagGDIOBJHDR
{
    HANDLE16    hNext;
    WORD        wMagic;
    DWORD       dwCount;
} GDIOBJHDR;


typedef struct
{
    WORD   version;       /*   0: driver version */
    WORD   technology;    /*   2: device technology */
    WORD   horzSize;      /*   4: width of display in mm */
    WORD   vertSize;      /*   6: height of display in mm */
    WORD   horzRes;       /*   8: width of display in pixels */
    WORD   vertRes;       /*  10: width of display in pixels */
    WORD   bitsPixel;     /*  12: bits per pixel */
    WORD   planes;        /*  14: color planes */
    WORD   numBrushes;    /*  16: device-specific brushes */
    WORD   numPens;       /*  18: device-specific pens */
    WORD   numMarkers;    /*  20: device-specific markers */
    WORD   numFonts;      /*  22: device-specific fonts */
    WORD   numColors;     /*  24: size of color table */
    WORD   pdeviceSize;   /*  26: size of PDEVICE structure */
    WORD   curveCaps;     /*  28: curve capabilities */
    WORD   lineCaps;      /*  30: line capabilities */
    WORD   polygonalCaps; /*  32: polygon capabilities */
    WORD   textCaps;      /*  34: text capabilities */
    WORD   clipCaps;      /*  36: clipping capabilities */
    WORD   rasterCaps;    /*  38: raster capabilities */
    WORD   aspectX;       /*  40: relative width of device pixel */
    WORD   aspectY;       /*  42: relative height of device pixel */
    WORD   aspectXY;      /*  44: relative diagonal width of device pixel */
    WORD   pad1[21];      /*  46-86: reserved */
    WORD   logPixelsX;    /*  88: pixels / logical X inch */
    WORD   logPixelsY;    /*  90: pixels / logical Y inch */
    WORD   pad2[6];       /*  92-102: reserved */
    WORD   sizePalette;   /* 104: entries in system palette */
    WORD   numReserved;   /* 106: reserved entries */
    WORD   colorRes;      /* 108: color resolution */    
} DeviceCaps;


  /* Device independent DC information */
typedef struct
{
    int           flags;
    const DeviceCaps *devCaps;

    HRGN16        hClipRgn;     /* Clip region (may be 0) */
    HRGN16        hVisRgn;      /* Visible region (must never be 0) */
    HRGN16        hGCClipRgn;   /* GC clip region (ClipRgn AND VisRgn) */
    HPEN16        hPen;
    HBRUSH16      hBrush;
    HFONT16       hFont;
    HBITMAP16     hBitmap;
    HBITMAP16     hFirstBitmap; /* Bitmap selected at creation of the DC */
    HANDLE16      hDevice;
    HPALETTE16    hPalette;

    WORD          ROPmode;
    WORD          polyFillMode;
    WORD          stretchBltMode;
    WORD          relAbsMode;
    WORD          backgroundMode;
    COLORREF      backgroundColor;
    COLORREF      textColor;
    int           backgroundPixel;
    int           textPixel;
    short         brushOrgX;
    short         brushOrgY;

    WORD          textAlign;         /* Text alignment from SetTextAlign() */
    short         charExtra;         /* Spacing from SetTextCharacterExtra() */
    short         breakTotalExtra;   /* Total extra space for justification */
    short         breakCount;        /* Break char. count */
    short         breakExtra;        /* breakTotalExtra / breakCount */
    short         breakRem;          /* breakTotalExtra % breakCount */

    BYTE          bitsPerPixel;

    WORD          MapMode;
    short         DCOrgX;            /* DC origin */
    short         DCOrgY;
    short         CursPosX;          /* Current position */
    short         CursPosY;
    short         WndOrgX;
    short         WndOrgY;
    short         WndExtX;
    short         WndExtY;
    short         VportOrgX;
    short         VportOrgY;
    short         VportExtX;
    short         VportExtY;
} WIN_DC_INFO;

typedef X11DRV_PDEVICE X_DC_INFO;  /* Temporary */

typedef struct tagDC
{
    GDIOBJHDR      header;
    HDC32          hSelf;            /* Handle to this DC */
    const struct tagDC_FUNCS *funcs; /* DC function table */
    void          *physDev;          /* Physical device (driver-specific) */
    WORD           saveLevel;
    DWORD          dwHookData;
    FARPROC16      hookProc;

    WIN_DC_INFO w;
    union
    {
	X_DC_INFO x;
	/* other devices (e.g. printer) */
    } u;
} DC;

/* Device functions for the Wine driver interface */
typedef struct tagDC_FUNCS
{
    BOOL32     (*pArc)(DC*,INT32,INT32,INT32,INT32,INT32,INT32,INT32,INT32);
    BOOL32     (*pBitBlt)(DC*,INT32,INT32,INT32,INT32,DC*,INT32,INT32,DWORD);
    BOOL32     (*pChord)(DC*,INT32,INT32,INT32,INT32,INT32,INT32,INT32,INT32);
    BOOL32     (*pCreateDC)(DC*,LPCSTR,LPCSTR,LPCSTR,const DEVMODE*);
    BOOL32     (*pDeleteDC)(DC*);
    BOOL32     (*pDeleteObject)(HGDIOBJ16);
    BOOL32     (*pEllipse)(DC*,INT32,INT32,INT32,INT32);
    INT32      (*pEscape)(DC*,INT32,INT32,SEGPTR,SEGPTR);
    INT32      (*pExcludeClipRect)(DC*,INT32,INT32,INT32,INT32);
    INT32      (*pExcludeVisRect)(DC*,INT32,INT32,INT32,INT32);
    BOOL32     (*pExtFloodFill)(DC*,INT32,INT32,COLORREF,UINT32);
    BOOL32     (*pExtTextOut)(DC*,INT32,INT32,UINT32,const RECT32*,LPCSTR,UINT32,const INT32*);
    BOOL32     (*pFillRgn)(DC*,HRGN32,HBRUSH32);
    BOOL32     (*pFloodFill)(DC*,INT32,INT32,COLORREF);
    BOOL32     (*pFrameRgn)(DC*,HRGN32,HBRUSH32,INT32,INT32);
    BOOL32     (*pGetTextExtentPoint)(DC*,LPCSTR,INT32,LPSIZE32);
    BOOL32     (*pGetTextMetrics)(DC*,TEXTMETRIC32A*);
    INT32      (*pIntersectClipRect)(DC*,INT32,INT32,INT32,INT32);
    INT32      (*pIntersectVisRect)(DC*,INT32,INT32,INT32,INT32);
    BOOL32     (*pInvertRgn)(DC*,HRGN32);
    BOOL32     (*pLineTo)(DC*,INT32,INT32);
    BOOL32     (*pMoveToEx)(DC*,INT32,INT32,LPPOINT32);
    INT32      (*pOffsetClipRgn)(DC*,INT32,INT32);
    BOOL32     (*pOffsetViewportOrgEx)(DC*,INT32,INT32,LPPOINT32);
    BOOL32     (*pOffsetWindowOrgEx)(DC*,INT32,INT32,LPPOINT32);
    BOOL32     (*pPaintRgn)(DC*,HRGN32);
    BOOL32     (*pPatBlt)(DC*,INT32,INT32,INT32,INT32,DWORD);
    BOOL32     (*pPie)(DC*,INT32,INT32,INT32,INT32,INT32,INT32,INT32,INT32);
    BOOL32     (*pPolyPolygon)(DC*,LPPOINT32,LPINT32,UINT32);
    BOOL32     (*pPolygon)(DC*,LPPOINT32,INT32);
    BOOL32     (*pPolyline)(DC*,LPPOINT32,INT32);
    UINT32     (*pRealizePalette)(DC*);
    BOOL32     (*pRectangle)(DC*,INT32,INT32,INT32,INT32);
    BOOL32     (*pRestoreDC)(DC*,INT32);
    BOOL32     (*pRoundRect)(DC*,INT32,INT32,INT32,INT32,INT32,INT32);
    INT32      (*pSaveDC)(DC*);
    BOOL32     (*pScaleViewportExtEx)(DC*,INT32,INT32,INT32,INT32,LPSIZE32);
    BOOL32     (*pScaleWindowExtEx)(DC*,INT32,INT32,INT32,INT32,LPSIZE32);
    INT32      (*pSelectClipRgn)(DC*,HRGN32);
    HANDLE32   (*pSelectObject)(DC*,HANDLE32);
    HPALETTE32 (*pSelectPalette)(DC*,HPALETTE32,BOOL32);
    COLORREF   (*pSetBkColor)(DC*,COLORREF);
    WORD       (*pSetBkMode)(DC*,WORD);
    VOID       (*pSetDeviceClipping)(DC*);
    INT32      (*pSetDIBitsToDevice)(DC*,INT32,INT32,DWORD,DWORD,INT32,INT32,UINT32,UINT32,LPCVOID,const BITMAPINFO*,UINT32);
    WORD       (*pSetMapMode)(DC*,WORD);
    DWORD      (*pSetMapperFlags)(DC*,DWORD);
    COLORREF   (*pSetPixel)(DC*,INT32,INT32,COLORREF);
    WORD       (*pSetPolyFillMode)(DC*,WORD);
    WORD       (*pSetROP2)(DC*,WORD);
    WORD       (*pSetRelAbs)(DC*,WORD);
    WORD       (*pSetStretchBltMode)(DC*,WORD);
    WORD       (*pSetTextAlign)(DC*,WORD);
    INT32      (*pSetTextCharacterExtra)(DC*,INT32);
    DWORD      (*pSetTextColor)(DC*,DWORD);
    INT32      (*pSetTextJustification)(DC*,INT32,INT32);
    BOOL32     (*pSetViewportExtEx)(DC*,INT32,INT32,LPSIZE32);
    BOOL32     (*pSetViewportOrgEx)(DC*,INT32,INT32,LPPOINT32);
    BOOL32     (*pSetWindowExtEx)(DC*,INT32,INT32,LPSIZE32);
    BOOL32     (*pSetWindowOrgEx)(DC*,INT32,INT32,LPPOINT32);
    BOOL32     (*pStretchBlt)(DC*,INT32,INT32,INT32,INT32,DC*,INT32,INT32,INT32,INT32,DWORD);
    INT32      (*pStretchDIBits)(DC*,INT32,INT32,INT32,INT32,INT32,INT32,INT32,INT32,LPSTR,LPBITMAPINFO,WORD,DWORD);
    BOOL32     (*pTextOut)(DC*,INT32,INT32,LPCSTR,INT32);
} DC_FUNCTIONS;

  /* DC hook codes */
#define DCHC_INVALIDVISRGN      0x0001
#define DCHC_DELETEDC           0x0002

#define DCHF_INVALIDATEVISRGN   0x0001
#define DCHF_VALIDATEVISRGN     0x0002

  /* DC flags */
#define DC_MEMORY     0x0001   /* It is a memory DC */
#define DC_SAVED      0x0002   /* It is a saved DC */
#define DC_DIRTY      0x0004   /* hVisRgn has to be updated */
#define DC_THUNKHOOK  0x0008   /* DC hook is in the 16-bit code */ 

  /* Last 32 bytes are reserved for stock object handles */
#define GDI_HEAP_SIZE               0xffe0

  /* First handle possible for stock objects (must be >= GDI_HEAP_SIZE) */
#define FIRST_STOCK_HANDLE          GDI_HEAP_SIZE

  /* Stock objects handles */

#define NB_STOCK_OBJECTS          (SYSTEM_FIXED_FONT + 1)

#define STOCK_WHITE_BRUSH       ((HBRUSH16)(FIRST_STOCK_HANDLE+WHITE_BRUSH))
#define STOCK_LTGRAY_BRUSH      ((HBRUSH16)(FIRST_STOCK_HANDLE+LTGRAY_BRUSH))
#define STOCK_GRAY_BRUSH        ((HBRUSH16)(FIRST_STOCK_HANDLE+GRAY_BRUSH))
#define STOCK_DKGRAY_BRUSH      ((HBRUSH16)(FIRST_STOCK_HANDLE+DKGRAY_BRUSH))
#define STOCK_BLACK_BRUSH       ((HBRUSH16)(FIRST_STOCK_HANDLE+BLACK_BRUSH))
#define STOCK_NULL_BRUSH        ((HBRUSH16)(FIRST_STOCK_HANDLE+NULL_BRUSH))
#define STOCK_HOLLOW_BRUSH      ((HBRUSH16)(FIRST_STOCK_HANDLE+HOLLOW_BRUSH))
#define STOCK_WHITE_PEN	        ((HPEN16)(FIRST_STOCK_HANDLE+WHITE_PEN))
#define STOCK_BLACK_PEN	        ((HPEN16)(FIRST_STOCK_HANDLE+BLACK_PEN))
#define STOCK_NULL_PEN	        ((HPEN16)(FIRST_STOCK_HANDLE+NULL_PEN))
#define STOCK_OEM_FIXED_FONT    ((HFONT16)(FIRST_STOCK_HANDLE+OEM_FIXED_FONT))
#define STOCK_ANSI_FIXED_FONT   ((HFONT16)(FIRST_STOCK_HANDLE+ANSI_FIXED_FONT))
#define STOCK_ANSI_VAR_FONT     ((HFONT16)(FIRST_STOCK_HANDLE+ANSI_VAR_FONT))
#define STOCK_SYSTEM_FONT       ((HFONT16)(FIRST_STOCK_HANDLE+SYSTEM_FONT))
#define STOCK_DEVICE_DEFAULT_FONT ((HFONT16)(FIRST_STOCK_HANDLE+DEVICE_DEFAULT_FONT))
#define STOCK_DEFAULT_PALETTE   ((HPALETTE16)(FIRST_STOCK_HANDLE+DEFAULT_PALETTE))
#define STOCK_SYSTEM_FIXED_FONT ((HFONT16)(FIRST_STOCK_HANDLE+SYSTEM_FIXED_FONT))

#define FIRST_STOCK_FONT        STOCK_OEM_FIXED_FONT
#define LAST_STOCK_FONT         STOCK_SYSTEM_FIXED_FONT

#define LAST_STOCK_HANDLE       ((DWORD)STOCK_SYSTEM_FIXED_FONT)

  /* Device <-> logical coords conversion */

#define XDPTOLP(dc,x) \
(((x)-(dc)->w.VportOrgX) * (dc)->w.WndExtX / (dc)->w.VportExtX+(dc)->w.WndOrgX)
#define YDPTOLP(dc,y) \
(((y)-(dc)->w.VportOrgY) * (dc)->w.WndExtY / (dc)->w.VportExtY+(dc)->w.WndOrgY)
#define XLPTODP(dc,x) \
(((x)-(dc)->w.WndOrgX) * (dc)->w.VportExtX / (dc)->w.WndExtX+(dc)->w.VportOrgX)
#define YLPTODP(dc,y) \
(((y)-(dc)->w.WndOrgY) * (dc)->w.VportExtY / (dc)->w.WndExtY+(dc)->w.VportOrgY)


  /* GDI local heap */

extern WORD GDI_HeapSel;

#define GDI_HEAP_ALLOC(size) \
            LOCAL_Alloc( GDI_HeapSel, LMEM_FIXED, (size) )
#define GDI_HEAP_REALLOC(handle,size) \
            LOCAL_ReAlloc( GDI_HeapSel, (handle), (size), LMEM_FIXED )
#define GDI_HEAP_FREE(handle) \
            LOCAL_Free( GDI_HeapSel, (handle) )
#define GDI_HEAP_LIN_ADDR(handle)  \
         ((handle) ? PTR_SEG_OFF_TO_LIN(GDI_HeapSel, (handle)) : NULL)
#define GDI_HEAP_SEG_ADDR(handle)  \
         ((handle) ? PTR_SEG_OFF_TO_SEGPTR(GDI_HeapSel, (handle)) : (SEGPTR)0)

extern BOOL32 GDI_Init(void);
extern HGDIOBJ16 GDI_AllocObject( WORD, WORD );
extern BOOL32 GDI_FreeObject( HGDIOBJ16 );
extern GDIOBJHDR * GDI_GetObjPtr( HGDIOBJ16, WORD );

extern BOOL32 DRIVER_RegisterDriver( LPCSTR name, const DC_FUNCTIONS *funcs );
extern const DC_FUNCTIONS *DRIVER_FindDriver( LPCSTR name );
extern BOOL32 DRIVER_UnregisterDriver( LPCSTR name );

extern Display * display;
extern Screen * screen;
extern Window rootWindow;
extern int screenWidth, screenHeight, screenDepth;
extern int desktopX, desktopY;   /* misc/main.c */

#endif  /* GDI_H */
