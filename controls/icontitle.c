/*
 * Icontitle window class.
 *
 * Copyright 1997 Alex Korobka
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "windef.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/winuser16.h"
#include "win.h"
#include "desktop.h"
#include "heap.h"

static  LPCSTR	emptyTitleText = "<...>";

	BOOL	bMultiLineTitle;
	HFONT	hIconTitleFont;

/***********************************************************************
 *           ICONTITLE_Init
 */
BOOL ICONTITLE_Init(void)
{
    LOGFONTA logFont;

    SystemParametersInfoA( SPI_GETICONTITLELOGFONT, 0, &logFont, 0 );
    SystemParametersInfoA( SPI_GETICONTITLEWRAP, 0, &bMultiLineTitle, 0 );
    hIconTitleFont = CreateFontIndirectA( &logFont );
    return (hIconTitleFont) ? TRUE : FALSE;
}

/***********************************************************************
 *           ICONTITLE_Create
 */
HWND ICONTITLE_Create( WND* wnd )
{
    WND* wndPtr;
    HWND hWnd;

    if( wnd->dwStyle & WS_CHILD )
	hWnd = CreateWindowExA( 0, ICONTITLE_CLASS_ATOM, NULL,
				  WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1,
				  wnd->parent->hwndSelf, 0, wnd->hInstance, NULL );
    else
	hWnd = CreateWindowExA( 0, ICONTITLE_CLASS_ATOM, NULL,
				  WS_CLIPSIBLINGS, 0, 0, 1, 1,
				  wnd->hwndSelf, 0, wnd->hInstance, NULL );
    wndPtr = WIN_FindWndPtr( hWnd );
    if( wndPtr )
    {
	wndPtr->owner = wnd;	/* MDI depends on this */
	wndPtr->dwStyle &= ~(WS_CAPTION | WS_BORDER);
	if( wnd->dwStyle & WS_DISABLED ) wndPtr->dwStyle |= WS_DISABLED;
        WIN_ReleaseWndPtr(wndPtr);
	return hWnd;
    }
    return 0;
}

/***********************************************************************
 *           ICONTITLE_GetTitlePos
 */
static BOOL ICONTITLE_GetTitlePos( WND* wnd, LPRECT lpRect )
{
    LPSTR str;
    int length = lstrlenA( wnd->owner->text );

    if( length )
    {
	str = HeapAlloc( GetProcessHeap(), 0, length + 1 );
	lstrcpyA( str, wnd->owner->text );
	while( str[length - 1] == ' ' ) /* remove trailing spaces */
	{ 
	    str[--length] = '\0';
	    if( !length )
	    {
		HeapFree( GetProcessHeap(), 0, str );
		break;
	    }
	}
    }
    if( !length ) 
    {
	str = (LPSTR)emptyTitleText;
	length = lstrlenA( str );
    }

    if( str )
    {
	HDC hDC = GetDC( wnd->hwndSelf );
	if( hDC )
	{
	    HFONT hPrevFont = SelectObject( hDC, hIconTitleFont );

	    SetRect( lpRect, 0, 0, GetSystemMetrics(SM_CXICONSPACING) -
		       GetSystemMetrics(SM_CXBORDER) * 2,
		       GetSystemMetrics(SM_CYBORDER) * 2 );

	    DrawTextA( hDC, str, length, lpRect, DT_CALCRECT |
			 DT_CENTER | DT_NOPREFIX | DT_WORDBREAK |
			 (( bMultiLineTitle ) ? 0 : DT_SINGLELINE) );

	    SelectObject( hDC, hPrevFont );
	    ReleaseDC( wnd->hwndSelf, hDC );

	    lpRect->right += 4 * GetSystemMetrics(SM_CXBORDER) - lpRect->left;
	    lpRect->left = wnd->owner->rectWindow.left + GetSystemMetrics(SM_CXICON) / 2 -
				      (lpRect->right - lpRect->left) / 2;
	    lpRect->bottom -= lpRect->top;
	    lpRect->top = wnd->owner->rectWindow.top + GetSystemMetrics(SM_CYICON);
	}
	if( str != emptyTitleText ) HeapFree( GetProcessHeap(), 0, str );
	return ( hDC ) ? TRUE : FALSE;
    }
    return FALSE;
}

/***********************************************************************
 *           ICONTITLE_Paint
 */
static BOOL ICONTITLE_Paint( WND* wnd, HDC hDC, BOOL bActive )
{
    HFONT hPrevFont;
    HBRUSH hBrush = 0;
    COLORREF textColor = 0;

    if( bActive )
    {
	hBrush = GetSysColorBrush(COLOR_ACTIVECAPTION);
	textColor = GetSysColor(COLOR_CAPTIONTEXT);
    }
    else 
    {
	if( wnd->dwStyle & WS_CHILD ) 
	{ 
	    hBrush = (HBRUSH) GetClassLongA(wnd->hwndSelf, GCL_HBRBACKGROUND);
	    if( hBrush )
	    {
		INT level;
		LOGBRUSH logBrush;
		GetObjectA( hBrush, sizeof(logBrush), &logBrush );
		level = GetRValue(logBrush.lbColor) +
			   GetGValue(logBrush.lbColor) +
			      GetBValue(logBrush.lbColor);
		if( level < (0x7F * 3) )
		    textColor = RGB( 0xFF, 0xFF, 0xFF );
	    }
	    else
		hBrush = GetStockObject( WHITE_BRUSH );
	}
	else
	{
	    hBrush = GetStockObject( BLACK_BRUSH );
	    textColor = RGB( 0xFF, 0xFF, 0xFF );    
	}
    }

    FillWindow16( wnd->parent->hwndSelf, wnd->hwndSelf, hDC, hBrush );

    hPrevFont = SelectObject( hDC, hIconTitleFont );
    if( hPrevFont )
    {
        RECT  rect;
	INT	length;
	char	buffer[80];

	rect.left = rect.top = 0;
	rect.right = wnd->rectWindow.right - wnd->rectWindow.left;
	rect.bottom = wnd->rectWindow.bottom - wnd->rectWindow.top;

	length = GetWindowTextA( wnd->owner->hwndSelf, buffer, 80 );
        SetTextColor( hDC, textColor );
        SetBkMode( hDC, TRANSPARENT );
	
	DrawTextA( hDC, buffer, length, &rect, DT_CENTER | DT_NOPREFIX |
		     DT_WORDBREAK | ((bMultiLineTitle) ? 0 : DT_SINGLELINE) ); 

	SelectObject( hDC, hPrevFont );
    }
    return ( hPrevFont ) ? TRUE : FALSE;
}

/***********************************************************************
 *           IconTitleWndProc
 */
LRESULT WINAPI IconTitleWndProc( HWND hWnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam )
{
    LRESULT retvalue;
    WND *wnd = WIN_FindWndPtr( hWnd );

    switch( msg )
    {
	case WM_NCHITTEST:
	     retvalue = HTCAPTION;
             goto END;
	case WM_NCMOUSEMOVE:
	case WM_NCLBUTTONDBLCLK:
	     retvalue = SendMessageA( wnd->owner->hwndSelf, msg, wParam, lParam );	
             goto END;
	case WM_ACTIVATE:
	     if( wParam ) SetActiveWindow( wnd->owner->hwndSelf );
	     /* fall through */

	case WM_CLOSE:
	     retvalue = 0;
             goto END;
	case WM_SHOWWINDOW:
	     if( wnd && wParam )
	     {
		 RECT titleRect;

		 ICONTITLE_GetTitlePos( wnd, &titleRect );
		 if( wnd->owner->next != wnd )	/* keep icon title behind the owner */
		     SetWindowPos( hWnd, wnd->owner->hwndSelf, 
				     titleRect.left, titleRect.top,
				     titleRect.right, titleRect.bottom, SWP_NOACTIVATE );
		 else
		     SetWindowPos( hWnd, 0, titleRect.left, titleRect.top,
				     titleRect.right, titleRect.bottom, 
				     SWP_NOACTIVATE | SWP_NOZORDER );
	     }
	     retvalue = 0;
             goto END;
	case WM_ERASEBKGND:
	     if( wnd )
	     {
		 WND* iconWnd = WIN_LockWndPtr(wnd->owner);

		 if( iconWnd->dwStyle & WS_CHILD )
		     lParam = SendMessageA( iconWnd->hwndSelf, WM_ISACTIVEICON, 0, 0 );
		 else
		     lParam = (iconWnd->hwndSelf == GetActiveWindow16());

                 WIN_ReleaseWndPtr(iconWnd);
		 if( ICONTITLE_Paint( wnd, (HDC)wParam, (BOOL)lParam ) )
		     ValidateRect( hWnd, NULL );
                 retvalue = 1;
                 goto END;
	     }
    }

    retvalue = DefWindowProcA( hWnd, msg, wParam, lParam );
END:
    WIN_ReleaseWndPtr(wnd);
    return retvalue;
}


