/* DRC VNC Viewer - a VNC client for the Wii U gamepad.
 *
 * Usage: drcvncviewer [-joystick] host[:port]
 *
 * Command Line Options:
 *
 * -joystick
 *   Enables the system input feeder, which forwards button & joystick presses
 *   to the PC as uinput events. Useful for gaming.
 *   With this option, use the gamepad POWER button to toggle between joystick
 *   and mouse modes.
 *
 * Prerequisites:
 *
 * -The vnc server must have dimensions 864x480, otherwise this segfaults.
 *   i.e. start vncserver with the geometry command as below
 *   $ vncserver :1 -geometry 864x480
 *   Otherwise it's segfault city.
 *
 * -The Wii U Gamepad is ready for PC control.
 *  1. It is paired with the hostapd running on your PC, and
 *  2. It has received an IP address from a DHCP server running on your PC
 *  If the gamepad can run the libdrc demo programs, it's ready.
 *
 * Control:
 *
 * Without the -joystick command line option, the gamepad is in mouse mode.
 * Touch the screen to move the mouse
 * Left click with ZL or ZR, and right click with L or R
 *
 * With the -joystick command line option, the gamepad starts in joystick mode.
 * Press the POWER button briefly to toggle between joystick and mouse mode.
 * In mouse mode the system input feeder is still active but feeding
 * idle/zero axis and button data to the system.
 *
 */

#include <SDL.h>
#include <signal.h>

#include <map>
//#include <string>
//#include <sstream>
//#include <vector>

#include <drc/input.h>
#include <drc/screen.h>
#include <drc/streamer.h>

extern "C" {
  #include <rfb/rfbclient.h>
}

void Init_DRC();
void Quit_DRC();
void Push_DRC_Frame(rfbClient *cl);

namespace {
  // libdrc globals
  drc::Streamer* g_streamer;

  // libSDL2 globals
  SDL_Window *sdlWindow;
  SDL_Renderer *sdlRenderer;
  SDL_Texture *sdlTexture;
  SDL_Surface *sdlSurface;

  // libvncclient globals
  static int viewOnly, listenLoop, buttonMask;
  static int bytesPerPixel;

  // keyboard handling globals
  SDL_Keycode symDown = 0;
  std::map <SDL_Keycode, char> keysDown;

  // other globals
  bool vncUpdate = 0;
  bool drcInputFeeder = FALSE; // set by command line
  bool drcJoystickMode = FALSE;
  static int frameRate = 60; // max FPS to DRC
}

static rfbBool resize(rfbClient* client) {
  int width=client->width,height=client->height,
    depth=client->format.bitsPerPixel;

  client->updateRect.x = client->updateRect.y = 0;
  client->updateRect.w = width; client->updateRect.h = height;

  sdlWindow = SDL_CreateWindow("DRC VNC Viewer",//client->desktopName,
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               width, height,
                               SDL_WINDOW_OPENGL);
  sdlRenderer = SDL_CreateRenderer(sdlWindow, -1,
                                   SDL_RENDERER_ACCELERATED);
//                                 SDL_RENDERER_SOFTWARE);

  sdlTexture = SDL_CreateTexture(sdlRenderer,
                                 SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width, height);

  SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);

  // blank the window
  SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
  SDL_RenderClear(sdlRenderer);

  SDL_RenderPresent(sdlRenderer);

  SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);

  uint8_t *pixels = static_cast<uint8_t*>
                      (malloc(drc::kScreenWidth * drc::kScreenHeight *
                      fmt->BytesPerPixel));
  SDL_RenderReadPixels(sdlRenderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels,
    drc::kScreenWidth * fmt->BytesPerPixel);
  client->frameBuffer=pixels;

  client->format.bitsPerPixel = fmt->BitsPerPixel;
  client->format.redShift     = fmt->Rshift;
  client->format.greenShift   = fmt->Gshift;
  client->format.blueShift    = fmt->Bshift;
  client->format.redMax       = fmt->Rmask>>client->format.redShift;
  client->format.greenMax     = fmt->Gmask>>client->format.greenShift;
  client->format.blueMax      = fmt->Bmask>>client->format.blueShift;
  SetFormatAndEncodings(client);

  return TRUE;
}

static rfbKeySym SDL_key2rfbKeySym(SDL_KeyboardEvent* e) {
  rfbKeySym k = 0;
  SDL_Keycode sym = e->keysym.sym;

  /* catch all the non-printable inputs which SDL_TEXTEVENT doesn't handle */
  switch (sym) {
    case SDLK_RETURN:           k = XK_Return; break;
    case SDLK_ESCAPE:           k = XK_Escape; break;
    case SDLK_BACKSPACE:        k = XK_BackSpace; break;
    case SDLK_TAB:              k = XK_Tab; break;

    case SDLK_CAPSLOCK:         k = XK_Caps_Lock; break;

    case SDLK_F1:               k = XK_F1; break;
    case SDLK_F2:               k = XK_F2; break;
    case SDLK_F3:               k = XK_F3; break;
    case SDLK_F4:               k = XK_F4; break;
    case SDLK_F5:               k = XK_F5; break;
    case SDLK_F6:               k = XK_F6; break;
    case SDLK_F7:               k = XK_F7; break;
    case SDLK_F8:               k = XK_F8; break;
    case SDLK_F9:               k = XK_F9; break;
    case SDLK_F10:              k = XK_F10; break;
    case SDLK_F11:              k = XK_F11; break;
    case SDLK_F12:              k = XK_F12; break;

    case SDLK_PRINTSCREEN:      k = XK_Print; break;
    case SDLK_SCROLLLOCK:       k = XK_Scroll_Lock; break;
    case SDLK_PAUSE:            k = XK_Pause; break;
    case SDLK_INSERT:           k = XK_Insert; break;
    case SDLK_HOME:             k = XK_Home; break;
    case SDLK_PAGEUP:           k = XK_Page_Up; break;
    case SDLK_DELETE:           k = XK_Delete; break;
    case SDLK_END:              k = XK_End; break;
    case SDLK_PAGEDOWN:         k = XK_Page_Down; break;
    case SDLK_RIGHT:            k = XK_Right; break;
    case SDLK_LEFT:             k = XK_Left; break;
    case SDLK_DOWN:             k = XK_Down; break;
    case SDLK_UP:               k = XK_Up; break;

    case SDLK_NUMLOCKCLEAR:     k = XK_Num_Lock; break;
    case SDLK_KP_ENTER:         k = XK_KP_Enter; break;

    case SDLK_F13:              k = XK_F13; break;
    case SDLK_F14:              k = XK_F14; break;
    case SDLK_F15:              k = XK_F15; break;
    case SDLK_F16:              k = XK_F16; break;
    case SDLK_F17:              k = XK_F17; break;
    case SDLK_F18:              k = XK_F18; break;
    case SDLK_F19:              k = XK_F19; break;
    case SDLK_F20:              k = XK_F20; break;
    case SDLK_F21:              k = XK_F21; break;
    case SDLK_F22:              k = XK_F22; break;
    case SDLK_F23:              k = XK_F23; break;
    case SDLK_F24:              k = XK_F24; break;
    case SDLK_EXECUTE:          k = XK_Execute; break;
    case SDLK_HELP:             k = XK_Help; break;
    case SDLK_MENU:             k = XK_Menu; break;
    case SDLK_SELECT:           k = XK_Select; break;
    case SDLK_STOP:             k = XK_Cancel; break;
    case SDLK_AGAIN:            k = XK_Redo; break;
    case SDLK_UNDO:             k = XK_Undo; break;
    case SDLK_FIND:             k = XK_Find; break;
    case SDLK_SYSREQ:           k = XK_Sys_Req; break;
    case SDLK_CLEAR:            k = XK_Clear; break;
    case SDLK_KP_TAB:           k = XK_KP_Tab; break;

    case SDLK_LCTRL:            k = XK_Control_L; break;
    case SDLK_LSHIFT:           k = XK_Shift_L; break;
    case SDLK_LALT:             k = XK_Alt_L; break;
    case SDLK_LGUI:             k = XK_Meta_L; break;
    case SDLK_RCTRL:            k = XK_Control_R; break;
    case SDLK_RSHIFT:           k = XK_Shift_R; break;
    case SDLK_RALT:             k = XK_Alt_R; break;
    case SDLK_RGUI:             k = XK_Meta_R; break;

    case SDLK_MODE:             k = XK_Mode_switch; break;

    default: break;
  }

  /* SDL_TEXTINPUT doesn't recognise key input when CTRL is pressed,
     so force those keys to be read as SDL keycodes in this case.
     SDL and rfb keysyms match in the ASCII range */
  if (SDL_GetModState() & KMOD_CTRL && sym < 0x100) k = sym;

  //if (k == 0) rfbClientLog("Unknown keysym: %d\n", sym);

  return k;
}

static void update(rfbClient* cl,int x,int y,int w,int h)
{
  vncUpdate = TRUE;
}

static void kbd_leds(rfbClient* cl, int value, int pad) {
  /* note: pad is for future expansion 0=unused */
  fprintf(stderr,"Led State= 0x%02X\n", value);
  fflush(stderr);
}

static void cleanup(rfbClient* cl)
{
  /*
    just in case we're running in listenLoop:
    close viewer window by restarting SDL video subsystem
  */
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  if(cl)
    rfbClientCleanup(cl);
}


static rfbBool handleSDLEvent(rfbClient *cl, SDL_Event *e)
{
  rfbKeySym rfbkey;

  switch(e->type) {

  case SDL_WINDOWEVENT:
    switch (e->window.event) {
      case SDL_WINDOWEVENT_EXPOSED:
        SendFramebufferUpdateRequest(cl, 0, 0, cl->width, cl->height, FALSE);
        break;
      default:
        break;
    }
    break;

  case SDL_MOUSEBUTTONUP:
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEMOTION:
  {
    int x, y, state, i;
    if (viewOnly)
      break;

    if (e->type == SDL_MOUSEMOTION) {
      x = e->motion.x;
      y = e->motion.y;
      state = e->motion.state;
    }
    else {
      x = e->button.x;
      y = e->button.y;
      state = e->button.button;
    }

    if (e->type == SDL_MOUSEBUTTONDOWN) buttonMask |= SDL_BUTTON(state);
    if (e->type == SDL_MOUSEBUTTONUP) buttonMask &= ~SDL_BUTTON(state);

    SendPointerEvent(cl, x, y, buttonMask);
    break;
  }

  case SDL_MOUSEWHEEL:
    // mouse coords need to be sent along with button presses
    int x, y;
    SDL_GetMouseState(&x, &y);

    if (e->wheel.y == 1) {
      buttonMask |= SDL_BUTTON(SDL_BUTTON_X1);
      SendPointerEvent(cl, x, y, buttonMask);
      buttonMask &= ~SDL_BUTTON(SDL_BUTTON_X1);
      SendPointerEvent(cl, x, y, buttonMask);
    }
    if (e->wheel.y == -1) {
      buttonMask |= SDL_BUTTON(SDL_BUTTON_X2);
      SendPointerEvent(cl, x, y, buttonMask);
      buttonMask &= ~SDL_BUTTON(SDL_BUTTON_X2);
      SendPointerEvent(cl, x, y, buttonMask);
    }
    break;

  case SDL_KEYDOWN:
    if (viewOnly)
      break;
    rfbkey = SDL_key2rfbKeySym(&e->key);
    if (rfbkey > 0) {
      SendKeyEvent(cl, rfbkey, TRUE);
    } else {
      symDown = e->key.keysym.sym;
    }
    break;
  case SDL_KEYUP:
    if (viewOnly)
      break;
    rfbkey = SDL_key2rfbKeySym(&e->key);
    if (rfbkey > 0) {
      SendKeyEvent(cl, rfbkey, FALSE);
    } else {
      SDL_Keycode sym = e->key.keysym.sym;
      if (keysDown.find(sym) != keysDown.end()) {
        SendKeyEvent(cl, keysDown[sym], FALSE);
        keysDown.erase(sym);
      }
    }
    break;
  case SDL_TEXTINPUT:
    keysDown[symDown] = e->text.text[0];
    symDown = 0;
    SendKeyEvent(cl, e->text.text[0], TRUE);
    break;

  case SDL_QUIT:
    if(listenLoop)
    {
      cleanup(cl);
      Quit_DRC();
      return FALSE;
    }
    else
    {
      rfbClientCleanup(cl);
      exit(0);
    }

  default:
    rfbClientLog("ignore SDL event: 0x%x\n", e->type);
  }
  return TRUE;
}

void Init_DRC() {
  g_streamer = new drc::Streamer();
  if (!g_streamer->Start()) {
    puts("Unable to start streamer");
    exit(1);
  }
}

void Quit_DRC() {
  if (g_streamer) {
    g_streamer->Stop();
  }
  delete g_streamer;
}

void Push_DRC_Frame(rfbClient *cl) {

  // Frame limiter
  static uint32_t startMS;
  if (startMS == 0)
    startMS = SDL_GetTicks();

  uint32_t elapsedMS;
  elapsedMS = SDL_GetTicks() - startMS; // time since last call

  uint32_t frameWaitMS = 1000 / frameRate;

  if (elapsedMS > frameWaitMS) {
  // push frame to DRC
    std::vector<drc::u8> pixels((drc::u8*)cl->frameBuffer,
                                (drc::u8*)cl->frameBuffer +
                                drc::kScreenWidth * drc::kScreenHeight * 4);
    g_streamer->PushVidFrame(&pixels, drc::kScreenWidth, drc::kScreenHeight,
                             drc::PixelFormat::kBGRA);

    startMS = SDL_GetTicks();
  }
}

void Process_DRC_Input(rfbClient *cl, drc::InputData& input_data) {

  static int prev_x, prev_y, prev_buttons;
  static int prev_lbutton, prev_rbutton, prev_pwrbutton;

  // BUG: pressing a button without having had a touchscreen event sends the
  //      cursor to (0,0) as prev_x, prev_y are uninitialised.

  // New unique button event
  if (input_data.buttons != prev_buttons) {
    int lbutton = input_data.buttons & (drc::InputData::kBtnZL | drc::InputData::kBtnZR);
    int rbutton = input_data.buttons & (drc::InputData::kBtnL | drc::InputData::kBtnR);
    int pwrbutton = input_data.buttons & drc::InputData::kBtnPower;

    // emulate mouse clicks with trigger buttons
    if (!drcJoystickMode) {
      if(lbutton != prev_lbutton) {
        if (lbutton)
          buttonMask |= rfbButton1Mask;
        else
          buttonMask &= ~rfbButton1Mask;
        SendPointerEvent(cl, prev_x, prev_y, buttonMask);
      }
      if(rbutton != prev_rbutton) {
        if (rbutton)
          buttonMask |= rfbButton3Mask;
        else
          buttonMask &= ~rfbButton3Mask;
        SendPointerEvent(cl, prev_x, prev_y, buttonMask);
      }
    }

    // if input feeder mode is on, push power button to toggle between
    // full joystick mode (all buttons go to input feeder) or
    // mouse only mode, where trigger buttons are mouse clicks
    if (drcInputFeeder) {
      if(pwrbutton != prev_pwrbutton) {
        if (pwrbutton) {
          drcJoystickMode = !drcJoystickMode;
          if (!drcJoystickMode) {
            printf("Mouse button mode\n");
            g_streamer->PauseSystemInputFeeder();
          } else {
            printf("Joystick mode\n");
            g_streamer->ResumeSystemInputFeeder();
          }
        }
      }
    }

    prev_lbutton = lbutton;
    prev_rbutton = rbutton;
    prev_pwrbutton = pwrbutton;

    prev_buttons = input_data.buttons;
  }

  // Handle touchscreen press
  if (input_data.ts_pressed) {
    int x = (input_data.ts_x * drc::kScreenWidth) - 1;
    int y = (input_data.ts_y * drc::kScreenHeight) - 1;

    if (x != prev_x || y != prev_y) {
      SendPointerEvent(cl, x, y, buttonMask);
      prev_x = x;
      prev_y = y;
    }
  }
}

int main(int argc,char** argv) {
  rfbClient* cl;
  int i, j;
  SDL_Event e;

  for (i = 1, j = 1; i < argc; i++)
    if (!strcmp(argv[i], "-viewonly"))
      viewOnly = 1;
    else if (!strcmp(argv[i], "-listen")) {
      listenLoop = 1;
      argv[i] = const_cast<char*>("-listennofork");
      ++j;
    }
    else if (!strcmp(argv[i], "-joystick")) {
      drcInputFeeder = TRUE;
    }
    else {
      if (i != j)
        argv[j] = argv[i];
      j++;
    }
  argc = j;

  Init_DRC();
  drc::InputData drc_input_data;

  if (drcInputFeeder) {
    printf("Started in Joystick mode, toggle Mouse mode with POWER button\n");
    g_streamer->EnableSystemInputFeeder();
    drcJoystickMode = 1;
  } else {
    printf("Started in Mouse-only mode\n");
    drcJoystickMode = 0;
  }

  SDL_Init(SDL_INIT_VIDEO);
  SDL_StartTextInput();

  atexit(SDL_Quit);
  signal(SIGINT, exit);

  do {
    /* 16-bit: cl=rfbGetClient(5,3,2); */
    cl=rfbGetClient(8,3,4);

    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    cl->format.bitsPerPixel = fmt->BitsPerPixel;
    cl->format.redShift     = fmt->Rshift;
    cl->format.greenShift   = fmt->Gshift;
    cl->format.blueShift    = fmt->Bshift;
    cl->format.redMax       = fmt->Rmask>>cl->format.redShift;
    cl->format.greenMax     = fmt->Gmask>>cl->format.greenShift;
    cl->format.blueMax      = fmt->Bmask>>cl->format.blueShift;

    cl->MallocFrameBuffer=resize;
    cl->canHandleNewFBSize = TRUE;
    cl->GotFrameBufferUpdate=update;
    cl->HandleKeyboardLedState=kbd_leds;
    cl->listenPort = LISTEN_PORT_OFFSET;
    cl->listen6Port = LISTEN_PORT_OFFSET;

    if(!rfbInitClient(cl,&argc,argv)) {
      cl = NULL; /* rfbInitClient has already freed the client struct */
      cleanup(cl);
      break;
    }

    while(1) {
      g_streamer->PollInput(&drc_input_data);
      if (drc_input_data.valid) {
        Process_DRC_Input(cl, drc_input_data);
      }
      if(SDL_PollEvent(&e)) {
      /*
        handleSDLEvent() return 0 if user requested window close.
        In this case, handleSDLEvent() will have called cleanup().
      */
        if(!handleSDLEvent(cl, &e))
          break;
      } else {
        i=WaitForMessage(cl,500);
        if(i<0) {
          cleanup(cl);
          break;
        }
        if(i) {
          if(!HandleRFBServerMessage(cl)) {
            cleanup(cl);
            break;
          }
        }
      }

      if (vncUpdate) {
        SDL_UpdateTexture(sdlTexture, NULL, cl->frameBuffer, drc::kScreenWidth * 4);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);
        Push_DRC_Frame(cl);
        vncUpdate = FALSE;
      }
    }
  } while(listenLoop);

  return 0;
}
