/* Functions taken directly from X sources for use with the Microsoft Windows API.
   Copyright (C) 1989, 1992-1995, 1999, 2001-2019 Free Software
   Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>
#include <signal.h>
#include <stdio.h>
#include <windows.h>
#include <windowsx.h>

#include "lisp.h"
#include "frame.h"
#include "w32term.h"

#define myalloc(cb) GlobalAllocPtr (GPTR, cb)
#define myfree(lp) GlobalFreePtr (lp)

CRITICAL_SECTION critsect;

#ifdef WINDOWSNT
extern HANDLE keyboard_handle;
#endif /* WINDOWSNT */

HANDLE input_available = NULL;
HANDLE interrupt_handle = NULL;

void
init_crit (void)
{
  InitializeCriticalSection (&critsect);

  /* For safety, input_available should only be reset by get_next_msg
     when the input queue is empty, so make it a manual reset event. */
  input_available = CreateEvent (NULL, TRUE, FALSE, NULL);

#if HAVE_W32NOTIFY
  /* Initialize the linked list of notifications sets that will be
     used to communicate between the watching worker threads and the
     main thread.  */
  notifications_set_head = malloc (sizeof(struct notifications_set));
  if (notifications_set_head)
    {
      memset (notifications_set_head, 0, sizeof(struct notifications_set));
      notifications_set_head->next
	= notifications_set_head->prev = notifications_set_head;
    }
  else
    DebPrint(("Out of memory: can't initialize notifications sets."));
#endif

#ifdef WINDOWSNT
  keyboard_handle = input_available;
#endif /* WINDOWSNT */

  /* interrupt_handle is signaled when quit (C-g) is detected, so that
     blocking system calls can be interrupted.  We make it a manual
     reset event, so that if we should ever have multiple threads
     performing system calls, they will all be interrupted (I'm guessing
     that would the right response).  Note that we use PulseEvent to
     signal this event, so that it never remains signaled.  */
  interrupt_handle = CreateEvent (NULL, TRUE, FALSE, NULL);
}

void
delete_crit (void)
{
  DeleteCriticalSection (&critsect);

  if (input_available)
    {
      CloseHandle (input_available);
      input_available = NULL;
    }
  if (interrupt_handle)
    {
      CloseHandle (interrupt_handle);
      interrupt_handle = NULL;
    }

#if HAVE_W32NOTIFY
  if (notifications_set_head)
    {
      /* Free any remaining notifications set that could be left over.  */
      while (notifications_set_head->next != notifications_set_head)
	{
	  struct notifications_set *ns = notifications_set_head->next;
	  notifications_set_head->next = ns->next;
	  ns->next->prev = notifications_set_head;
	  if (ns->notifications)
	    free (ns->notifications);
	  free (ns);
	}
    }
  free (notifications_set_head);
#endif
}

void
signal_quit (void)
{
  /* Make sure this event never remains signaled; if the main thread
     isn't in a blocking call, then this should do nothing.  */
  PulseEvent (interrupt_handle);
}

void
select_palette (struct frame *f, HDC hdc)
{
  struct w32_display_info *display_info = FRAME_DISPLAY_INFO (f);

  if (!display_info->has_palette)
    return;

  if (display_info->palette == 0)
    return;

  if (!NILP (Vw32_enable_palette))
    f->output_data.w32->old_palette =
      SelectPalette (hdc, display_info->palette, FALSE);
  else
    f->output_data.w32->old_palette = NULL;

  if (RealizePalette (hdc) != GDI_ERROR)
  {
    Lisp_Object frame, framelist;
    FOR_EACH_FRAME (framelist, frame)
    {
      SET_FRAME_GARBAGED (XFRAME (frame));
    }
  }
}

void
deselect_palette (struct frame *f, HDC hdc)
{
  if (f->output_data.w32->old_palette)
    SelectPalette (hdc, f->output_data.w32->old_palette, FALSE);
}

/* Get a DC for frame and select palette for drawing; force an update of
   all frames if palette's mapping changes.

   When using double buffered mode, DCs are stored on the frame data
   after the first call and are never released.
*/

HDC get_frame_dc1(struct frame * f, const char *caller_name)
{

  printf( "Caller: %s\n", caller_name );
  if (!strcmp( caller_name, "draw_relief_rect" ))
    printf( "!!!!!!!!!!!!!!!!!!!" );
  return get_frame_dc__( f );
}

int release_frame_dc1(struct frame * f, HDC hDC)
{
  return release_frame_dc__( f, hDC );
}


HDC
get_frame_dc__ (struct frame *f)
{
  printf( "get_frame_dc %p\n", f );
  fflush(stdout);

  if (f->output_method != output_w32)
    emacs_abort ();

  enter_crit ();
  if (one_w32_display_info.drawing_mode == W32_DRAWING_MODE_DIRECT2D )
    {
      if (f->output_data.w32->hdc == NULL)
        {
          if (f->output_data.w32->window_desc)
            {
              if ( f->output_data.w32->d2d_render_target == NULL )
                {
                  /* Initialize Direct2D */
                  dummy_D2D1_RENDER_TARGET_PROPERTIES props =
                    {
                     dummy_D2D1_RENDER_TARGET_TYPE_DEFAULT,
                     { dummy_DXGI_FORMAT_B8G8R8A8_UNORM, dummy_D2D1_ALPHA_MODE_PREMULTIPLIED },
                     0.0f, 0.0f,
                     dummy_D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
                     dummy_D2D1_FEATURE_LEVEL_DEFAULT
                    };


                  RECT rc;
                  GetClientRect( f->output_data.w32->window_desc, &rc );

                  dummy_D2D1_HWND_RENDER_TARGET_PROPERTIES props2 =
                    {
                     f->output_data.w32->window_desc,
                     { rc.right - rc.left,    rc.bottom - rc.top },
                     dummy_D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
                    };


                  HRESULT hr = dummy_ID2D1Factory_CreateHwndRenderTarget(one_w32_display_info.d2d_factory,
                                                                         &props,
                                                                         &props2,
                                                                         (dummy_ID2D1HwndRenderTarget**)&f->output_data.w32->d2d_render_target);
                  if(FAILED(hr)) {
                    printf( "CREATEDCRENDERTRGET %ld %p", hr, f->output_data.w32->window_desc );
                    fflush(stdout);
                    abort();
                  }
                }



              HRESULT hr = dummy_ID2D1HwndRenderTarget_QueryInterface( f->output_data.w32->d2d_render_target,
                                                               &dummy_IID_ID2D1GdiInteropRenderTarget,
                                                               (void**)&f->output_data.w32->d2d_gdi_interop_render_target );
              if(FAILED(hr)) {
                printf( "Query interface failed" );
                fflush(stdout);
                abort();
              }

              dummy_ID2D1RenderTarget_BeginDraw(f->output_data.w32->d2d_render_target);

              HDC hdc;

              hr = dummy_ID2D1GdiInteropRenderTarget_GetDC( f->output_data.w32->d2d_gdi_interop_render_target,
                                                            dummy_D2D1_DC_INITIALIZE_MODE_COPY,
                                                            &hdc );
              if(FAILED(hr)) {
                printf( "GetDC Failed" );
                fflush(stdout);
                abort();
              }

              f->output_data.w32->hdc = hdc;
              return hdc;

              /* f->output_data.w32->back_hdc = CreateCompatibleDC(hdc); */
              /* f->output_data.w32->back_bitmap = CreateCompatibleBitmap( hdc, 2000, 2000 ); */
              /* SelectObject( f->output_data.w32->back_hdc, f->output_data.w32->back_bitmap ); */
            }
          else
            {
              // DC for NULL window.
              HDC hdc = GetDC (f->output_data.w32->window_desc);
              return hdc;
            }

        }
      return f->output_data.w32->hdc;
    }
  else
    {
      HDC hdc = GetDC (f->output_data.w32->window_desc);
      /* If this gets called during startup before the frame is valid,
         there is a chance of corrupting random data or crashing. */
      if (hdc)
        select_palette (f, hdc);
      return hdc;
    }
}


int
release_frame_dc__ (struct frame *f, HDC hdc)
{
  int ret;
  if (one_w32_display_info.drawing_mode == W32_DRAWING_MODE_DIRECT2D )
    {
      ret = 1;
      // This dc was created when there was still no window. Have to release it.
      if (f->output_data.w32->window_desc == NULL)
        {
          ret = ReleaseDC(f->output_data.w32->window_desc, hdc);
        }
    }
  else
    {
      deselect_palette (f, hdc);
      ret = ReleaseDC (f->output_data.w32->window_desc, hdc);
    }
  leave_crit ();

  return ret;
}

typedef struct int_msg
{
  W32Msg w32msg;
  struct int_msg *lpNext;
} int_msg;

int_msg *lpHead = NULL;
int_msg *lpTail = NULL;
int nQueue = 0;

BOOL
get_next_msg (W32Msg * lpmsg, BOOL bWait)
{
  BOOL bRet = FALSE;

  enter_crit ();

  /* The while loop takes care of multiple sets */

  while (!nQueue && bWait)
    {
      leave_crit ();
      WaitForSingleObject (input_available, INFINITE);
      enter_crit ();
    }

  if (nQueue)
    {
      memcpy (lpmsg, &lpHead->w32msg, sizeof (W32Msg));

      {
	int_msg * lpCur = lpHead;

	lpHead = lpHead->lpNext;

	myfree (lpCur);
      }

      nQueue--;
      /* Consolidate WM_PAINT messages to optimize redrawing.  */
      if (lpmsg->msg.message == WM_PAINT && nQueue)
        {
          int_msg * lpCur = lpHead;
          int_msg * lpPrev = NULL;
          int_msg * lpNext = NULL;

          while (lpCur && nQueue)
            {
              lpNext = lpCur->lpNext;
              if (lpCur->w32msg.msg.message == WM_PAINT)
                {
                  /* Remove this message from the queue.  */
                  if (lpPrev)
                    lpPrev->lpNext = lpNext;
                  else
                    lpHead = lpNext;

                  if (lpCur == lpTail)
                    lpTail = lpPrev;

                  /* Adjust clip rectangle to cover both.  */
                  if (!UnionRect (&(lpmsg->rect), &(lpmsg->rect),
                                  &(lpCur->w32msg.rect)))
                    {
                      SetRectEmpty (&(lpmsg->rect));
                    }

                  myfree (lpCur);

                  nQueue--;

                  lpCur = lpNext;
                }
              else
                {
                  lpPrev = lpCur;
                  lpCur = lpNext;
                }
            }
        }

      bRet = TRUE;
    }

  if (nQueue == 0)
    ResetEvent (input_available);

  leave_crit ();

  return (bRet);
}

extern char * w32_strerror (int error_no);

/* Tell the main thread that we have input available; if the main
   thread is blocked in select(), we wake it up here.  */
static void
notify_msg_ready (void)
{
  SetEvent (input_available);

#ifdef CYGWIN
  /* Wakes up the main thread, which is blocked select()ing for /dev/windows,
     among other files.  */
  (void) PostThreadMessage (dwMainThreadId, WM_EMACS_INPUT_READY, 0, 0);
#endif /* CYGWIN */
}

BOOL
post_msg (W32Msg * lpmsg)
{
  int_msg * lpNew = (int_msg *) myalloc (sizeof (int_msg));

  if (!lpNew)
    return (FALSE);

  memcpy (&lpNew->w32msg, lpmsg, sizeof (W32Msg));
  lpNew->lpNext = NULL;

  enter_crit ();

  if (nQueue++)
    {
      lpTail->lpNext = lpNew;
    }
  else
    {
      lpHead = lpNew;
    }

  lpTail = lpNew;
  notify_msg_ready ();
  leave_crit ();

  return (TRUE);
}

BOOL
prepend_msg (W32Msg *lpmsg)
{
  int_msg * lpNew = (int_msg *) myalloc (sizeof (int_msg));

  if (!lpNew)
    return (FALSE);

  memcpy (&lpNew->w32msg, lpmsg, sizeof (W32Msg));

  enter_crit ();

  nQueue++;
  lpNew->lpNext = lpHead;
  lpHead = lpNew;
  notify_msg_ready ();
  leave_crit ();

  return (TRUE);
}

/* Process all messages in the current thread's queue.  Value is 1 if
   one of these messages was WM_EMACS_FILENOTIFY, zero otherwise.  */
int
drain_message_queue (void)
{
  MSG msg;
  int retval = 0;

  while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    {
      if (msg.message == WM_EMACS_FILENOTIFY)
	retval = 1;
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }
  return retval;
}
