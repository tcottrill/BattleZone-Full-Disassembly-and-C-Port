//Some code for this taken from
//https://blog.molecular-matters.com/2011/09/05/properly-handling-keyboard-input/
//

#include "rawinput.h"
#include <stdbool.h>


static HWND ihwnd;
unsigned char key[256];
static unsigned int lastkey[256];
int mouse_b;


struct DXTI_MOUSE_STATE
{
	long x, y, wheel; //current position
	long dx, dy, dwheel; //change in position
	bool left, middle, right; //buttons
};


enum DXTI_MOUSE_BUTTON_STATE //named state of mouse buttons
{
	UP = FALSE,
	DOWN = TRUE,
};

static struct DXTI_MOUSE_STATE m_mouseStateRaw;

// Previous position reported by an absolute-coordinate mouse device, used to
// turn its reports back into deltas.
static LONG last_absolute_x;
static LONG last_absolute_y;
static int have_absolute;



HRESULT RawInput_Initialize(HWND hWnd)
{
	RAWINPUTDEVICE Rid[2];

	Rid[0].usUsagePage = 0x01;	// mouse
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = 0;			//RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK;
	Rid[0].hwndTarget = hWnd;

	Rid[1].usUsagePage = 0x01;	// keyboard
	Rid[1].usUsage = 0x06;
	Rid[1].dwFlags = 0;
	Rid[1].hwndTarget = hWnd;

	ZeroMemory(key, sizeof(key));
	ZeroMemory(lastkey, sizeof(lastkey));
	ZeroMemory(&m_mouseStateRaw, sizeof(m_mouseStateRaw));
	last_absolute_x = 0;
	last_absolute_y = 0;
	have_absolute = 0;

	ShowCursor(1);
	ihwnd = hWnd;
	if (FALSE == RegisterRawInputDevices(Rid, 2, sizeof(Rid[0]))) //registers both mouse and keyboard
		return E_FAIL;

	return S_OK;
}

LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	(void)hWnd;

	if (RIM_INPUTSINK == wParam) return 0;

	RAWINPUT input;
	UINT nSize = sizeof(input);

	if (GetRawInputData((HRAWINPUT)lParam,
		RID_INPUT,
		&input,
		&nSize,
		sizeof(input.header)) == (UINT)-1)
	{
		return 0;
	}

	switch (input.header.dwType)  //input.header.hDevice is the individual device name creating the keystrokes
	{
	case RIM_TYPEKEYBOARD: //this message only occurs when the keyboard is registered for raw input
	{
		UINT virtualKey = input.data.keyboard.VKey;
		UINT scanCode = input.data.keyboard.MakeCode;
		UINT flags = input.data.keyboard.Flags;

		if (virtualKey == 255)
		{
			// discard "fake keys" which are part of an escaped sequence
			break;
		}
		else if (virtualKey == VK_SHIFT)
		{
			// correct left-hand / right-hand SHIFT. MapVirtualKey returns 0
			// when it cannot translate the scan code; keeping VK_SHIFT is far
			// better than falling through and writing to key[0].
			UINT mapped = MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
			if (mapped) virtualKey = mapped;
		}

		// e0 and e1 are escape sequences used for certain special keys, such as PRINT and PAUSE/BREAK.
		// see http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
		const bool isE0 = ((flags & RI_KEY_E0) != 0);

		switch (virtualKey)
		{
			// right-hand CONTROL and ALT have their e0 bit set
		case VK_CONTROL:
			virtualKey = isE0 ? VK_RCONTROL : VK_LCONTROL;
			break;

		case VK_MENU:
			virtualKey = isE0 ? VK_RMENU : VK_LMENU;
			break;

			// NUMPAD ENTER has its e0 bit set
		case VK_RETURN:
			if (isE0)
				virtualKey = VK_SEPARATOR;
			break;

			// the standard INSERT, DELETE, HOME, END, PRIOR and NEXT keys will always have their e0 bit set, but the
			// corresponding keys on the NUMPAD will not.
		case VK_INSERT:
			if (!isE0)
				virtualKey = VK_NUMPAD0;
			break;

		case VK_DELETE:
			if (!isE0)
				virtualKey = VK_DECIMAL;
			break;

		case VK_HOME:
			if (!isE0)
				virtualKey = VK_NUMPAD7;
			break;

		case VK_END:
			if (!isE0)
				virtualKey = VK_NUMPAD1;
			break;

		case VK_PRIOR:
			if (!isE0)
				virtualKey = VK_NUMPAD9;
			break;

		case VK_NEXT:
			if (!isE0)
				virtualKey = VK_NUMPAD3;
			break;

			// the standard arrow keys will always have their e0 bit set, but the
			// corresponding keys on the NUMPAD will not.
		case VK_LEFT:
			if (!isE0)
				virtualKey = VK_NUMPAD4;
			break;

		case VK_RIGHT:
			if (!isE0)
				virtualKey = VK_NUMPAD6;
			break;

		case VK_UP:
			if (!isE0)
				virtualKey = VK_NUMPAD8;
			break;

		case VK_DOWN:
			if (!isE0)
				virtualKey = VK_NUMPAD2;
			break;

			// NUMPAD 5 doesn't have its e0 bit set
		case VK_CLEAR:
			if (!isE0)
				virtualKey = VK_NUMPAD5;
			break;
		}

		virtualKey &= 0xff;

		if (!(flags & RI_KEY_BREAK)) //Is the key down or up?
		{
			key[virtualKey] = 0x80;
			//Count repeats, guarding against wraparound back to "not held"
			if (lastkey[virtualKey] < 0xfffffffe) lastkey[virtualKey] += 1;
		}
		else
		{
			key[virtualKey] = 0x00;
			lastkey[virtualKey] = 0x00;
		}

		break;
	}

	case RIM_TYPEMOUSE:
	{
		//Button flags can arrive combined in one packet, so test each bit
		USHORT bflags = input.data.mouse.usButtonFlags;

		if (bflags & RI_MOUSE_LEFT_BUTTON_DOWN)   m_mouseStateRaw.left = DOWN;
		if (bflags & RI_MOUSE_LEFT_BUTTON_UP)     m_mouseStateRaw.left = UP;
		if (bflags & RI_MOUSE_RIGHT_BUTTON_DOWN)  m_mouseStateRaw.right = DOWN;
		if (bflags & RI_MOUSE_RIGHT_BUTTON_UP)    m_mouseStateRaw.right = UP;
		if (bflags & RI_MOUSE_MIDDLE_BUTTON_DOWN) m_mouseStateRaw.middle = DOWN;
		if (bflags & RI_MOUSE_MIDDLE_BUTTON_UP)   m_mouseStateRaw.middle = UP;
		if (bflags & RI_MOUSE_WHEEL)
		{
			//Wheel delta is signed, stored in an unsigned field
			short wheelDelta = (short)input.data.mouse.usButtonData;
			m_mouseStateRaw.dwheel += wheelDelta;
			m_mouseStateRaw.wheel += wheelDelta;
		}

		// Several WM_INPUT packets can arrive between two frames, so motion
		// must accumulate. Assigning here discarded everything but the last
		// packet, which under-reported fast movement.
		if (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
		{
			// RDP sessions, tablets and some VM guest drivers report absolute
			// virtual-desktop coordinates rather than motion. Treating those
			// as deltas sent the position off to infinity on the first packet.
			LONG ax = input.data.mouse.lLastX;
			LONG ay = input.data.mouse.lLastY;

			if (have_absolute)
			{
				m_mouseStateRaw.dx += ax - last_absolute_x;
				m_mouseStateRaw.dy += ay - last_absolute_y;
				m_mouseStateRaw.x += ax - last_absolute_x;
				m_mouseStateRaw.y += ay - last_absolute_y;
			}

			last_absolute_x = ax;
			last_absolute_y = ay;
			have_absolute = 1;
		}
		else
		{
			m_mouseStateRaw.dx += input.data.mouse.lLastX;
			m_mouseStateRaw.dy += input.data.mouse.lLastY;
			m_mouseStateRaw.x += input.data.mouse.lLastX;
			m_mouseStateRaw.y += input.data.mouse.lLastY;
		}

		if (m_mouseStateRaw.left)   bset(mouse_b, 0x01); else bclr(mouse_b, 0x01);
		if (m_mouseStateRaw.right)  bset(mouse_b, 0x02); else bclr(mouse_b, 0x02);
		if (m_mouseStateRaw.middle) bset(mouse_b, 0x04); else bclr(mouse_b, 0x04);
		break;
	}
	}
	return 0;
}

void get_mouse_win(int *mickeyx, int *mickeyy)
{
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(ihwnd, &cursor_pos);
	*mickeyx = cursor_pos.x;
	*mickeyy = cursor_pos.y;
}

void get_mouse_mickeys(int *mickeyx, int *mickeyy)
{
	*mickeyx = m_mouseStateRaw.dx;
	*mickeyy = m_mouseStateRaw.dy;

	m_mouseStateRaw.dx = 0;
	m_mouseStateRaw.dy = 0;
}

//keyboard state checks
int isKeyHeld(INT vkCode) { return lastkey[vkCode & 0xff]; }
BOOL IsKeyDown(INT vkCode) { return key[vkCode & 0xff] ? TRUE : FALSE; }
BOOL IsKeyUp(INT vkCode) { return key[vkCode & 0xff] ? FALSE : TRUE; }

//summed mouse state checks/sets;
//use as convenience, ie. keeping track of movements without needing to maintain separate data set
//naming is left to C style for compatibility
LONG GetMouseX(void) { return m_mouseStateRaw.x; }
LONG GetMouseY(void) { return m_mouseStateRaw.y; }
LONG GetMouseWheel(void) { return m_mouseStateRaw.wheel; }
void SetMouseX(LONG x) { m_mouseStateRaw.x = x; }
void SetMouseY(LONG y) { m_mouseStateRaw.y = y; }
void SetMouseWheel(LONG wheel) { m_mouseStateRaw.wheel = wheel; }

//relative mouse state changes. These peek at the same accumulators that
//get_mouse_mickeys() drains, so whichever you use, use only one.
LONG GetMouseXChange(void) { return m_mouseStateRaw.dx; }
LONG GetMouseYChange(void) { return m_mouseStateRaw.dy; }
LONG GetMouseWheelChange(void) { return m_mouseStateRaw.dwheel; }

//mouse button state checks
BOOL IsMouseLButtonDown(void) { return (m_mouseStateRaw.left == DOWN) ? TRUE : FALSE; }
BOOL IsMouseLButtonUp(void) { return (m_mouseStateRaw.left == UP) ? TRUE : FALSE; }
BOOL IsMouseRButtonDown(void) { return (m_mouseStateRaw.right == DOWN) ? TRUE : FALSE; }
BOOL IsMouseRButtonUp(void) { return (m_mouseStateRaw.right == UP) ? TRUE : FALSE; }
BOOL IsMouseMButtonDown(void) { return (m_mouseStateRaw.middle == DOWN) ? TRUE : FALSE; }
BOOL IsMouseMButtonUp(void) { return (m_mouseStateRaw.middle == UP) ? TRUE : FALSE; }
