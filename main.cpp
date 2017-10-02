#include <windows.h>
#include <tchar.h>
#include <vector>
#include <limits>
#include <iostream>
#include <algorithm>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"
#include "our_gl.h"

 
Model *model        = NULL;

const int width  = 800;
const int height = 800;

Vec3f light_dir(1,1,1);
Vec3f       eye(1,1,3);
Vec3f    center(0,0,0);
Vec3f        up(0,1,0);

struct Shader : public IShader {
    mat<2,3,float> varying_uv;  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    mat<4,3,float> varying_tri; // triangle coordinates (clip coordinates), written by VS, read by FS
    mat<3,3,float> varying_nrm; // normal per vertex to be interpolated by FS
    mat<3,3,float> ndc_tri;     // triangle in normalized device coordinates

    virtual Vec4f vertex(int iface, int nthvert) {
        varying_uv.set_col(nthvert, model->uv(iface, nthvert));
        varying_nrm.set_col(nthvert, proj<3>((Projection*ModelView).invert_transpose()*embed<4>(model->normal(iface, nthvert), 0.f)));
        Vec4f gl_Vertex = Projection*ModelView*embed<4>(model->vert(iface, nthvert));
        varying_tri.set_col(nthvert, gl_Vertex);
        ndc_tri.set_col(nthvert, proj<3>(gl_Vertex/gl_Vertex[3]));
        return gl_Vertex;
    }

    virtual bool fragment(Vec3f bar, TGAColor &color) {
        Vec3f bn = (varying_nrm*bar).normalize();
        Vec2f uv = varying_uv*bar;

        mat<3,3,float> A;
        A[0] = ndc_tri.col(1) - ndc_tri.col(0);
        A[1] = ndc_tri.col(2) - ndc_tri.col(0);
        A[2] = bn;

        mat<3,3,float> AI = A.invert();

        Vec3f i = AI * Vec3f(varying_uv[0][1] - varying_uv[0][0], varying_uv[0][2] - varying_uv[0][0], 0);
        Vec3f j = AI * Vec3f(varying_uv[1][1] - varying_uv[1][0], varying_uv[1][2] - varying_uv[1][0], 0);

        mat<3,3,float> B;
        B.set_col(0, i.normalize());
        B.set_col(1, j.normalize());
        B.set_col(2, bn);

        Vec3f n = (B*model->normal(uv)).normalize();

        float diff = (std::max)(0.f, n*light_dir);
        color = model->diffuse(uv)*diff;

        return false;
    }
};

HDC memDC;

HDC TGA_To_MemDC (TGAImage* tga )
{
	// If we're not trying to create a 24-bit or 32-bit CTarga, there is 
	// no need to go on
	int channels = tga->get_bytespp();
	if(!(channels == 3 || channels == 4))
		return 0;
	

	// Set the stride

	int stride = tga->get_width() * channels;

	while((stride % 4) != 0) // Ensure stride is DWORD aligned
		stride++;

	BITMAPINFO bmp_info = {0};

	// Initialize the parameters that we care about
	bmp_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmp_info.bmiHeader.biWidth = tga->get_width();
	bmp_info.bmiHeader.biHeight = tga->get_height();
	bmp_info.bmiHeader.biPlanes = 1; // Must be set to one
	bmp_info.bmiHeader.biBitCount = channels * 8;
	bmp_info.bmiHeader.biCompression = BI_RGB; // Bitmap is NOT compressed	


	// This will create an our hbitmap with the properties we desire -- By parameter
	// hdc -- This is HDC we want to use to determine how we're going to
	//        initialize the hbitmap's colors
	// bmp_info -- Pointer to the BITMAPINFO that contains the various
	//             attributes of the hbitmap we want to create
	// DIB_RGB_COLORS -- Means the BITMAPINFO struct contains an array of actual colors.
	//					 In other words NO PALLETES
	// surface_bits	-- Pointer to a pointer to receive the address of hbitmap's pixel bits
	// NULL -- Handle to a file mapping object the function will use to create the hbitmap
	//		   NULL means no file mapping object
	// NULL -- An offset for the file mapping object, if the handle to the file mapping object
	//		   is NULL, this parameter is ignored
	unsigned char* buf = tga->buffer();

	HDC dc = GetDC(GetDesktopWindow());
	HBITMAP hbitmap = CreateCompatibleBitmap(dc, width, height);
	SetDIBits(dc, hbitmap, 0, height, (const void*)tga->buffer(), &bmp_info, DIB_RGB_COLORS);
	ReleaseDC(GetDesktopWindow(), dc);
	
	// Create a compatible DC with whatever the current display mode is
	HDC hdc = CreateCompatibleDC(NULL);

	// Error Check
	if(!hdc)
		return 0;

	// Select our DIBSection bitmap into our HDC.
	// This allows us to draw to the bitmap using GDI functions.
	SelectObject(hdc, hbitmap);

	return hdc;

} // end of init(int width, int height, int channels)


LRESULT CALLBACK AppHandler (HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam) {
	switch (Message)
	{

	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		BitBlt(ps.hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
		EndPaint(hwnd, &ps);
		break;
	}

	/* Upon destruction, tell the main thread to stop */
	case WM_DESTROY: {
		PostQuitMessage(0);
		break;
	}

	/* All other messages (a lot of them) are processed using default procedures */
	default:
		return DefWindowProc(hwnd, Message, wParam, lParam);
}
return 0;
}

static const TCHAR* AppClassName = _T("TinyRender");
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {


    float *zbuffer = new float[width*height];
    for (int i=width*height; i--; zbuffer[i] = -(std::numeric_limits<float>::max)());

    TGAImage frame(width, height, TGAImage::RGB);
    lookat(eye, center, up);
    viewport(width/8, height/8, width*3/4, height*3/4);
    projection(-1.f/(eye-center).norm());
    light_dir = proj<3>((Projection*ModelView*embed<4>(light_dir, 0.f))).normalize();


    model = new Model("obj\\african_head\\african_head.obj");
    Shader shader;
    for (int i=0; i<model->nfaces(); i++) {
        for (int j=0; j<3; j++) {
            shader.vertex(i, j);
        }
        triangle(shader.varying_tri, shader, frame, zbuffer);
    }
    delete model;

	memDC = TGA_To_MemDC(&frame);

    frame.flip_vertically(); // to place the origin in the bottom left corner of the image
    frame.write_tga_file("framebuffer.tga");

	// Application class registration
	MSG Msg;
	RECT R;
	WNDCLASSEX WndClass = { 0 };									// Zero the class record
	WndClass.cbSize = sizeof(WNDCLASSEX);							// Size of this record
	WndClass.lpfnWndProc = AppHandler;								// Handler for this class
	WndClass.cbClsExtra = 0;										// No extra class data
	WndClass.cbWndExtra = 0;										// No extra window data
	WndClass.hInstance = GetModuleHandle(NULL);						// This instance
	WndClass.hIcon = LoadIcon(0, IDI_APPLICATION);					// Set icon
	WndClass.hCursor = LoadCursor(0, IDC_ARROW);					// Set cursor
	WndClass.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);	// Set background brush
	WndClass.lpszMenuName = 0;										// No menu
	WndClass.lpszClassName = AppClassName;							// Set class name
	RegisterClassEx(&WndClass);										// Register the class

	GetClientRect(GetDesktopWindow(), &R);							// Get desktop area
	R.left = (R.right + R.left - width) / 2;
	R.top = (R.top + R.bottom - height) / 2;
	CreateWindowEx(0, AppClassName, _T("Software Render Example"),
		WS_VISIBLE | WS_OVERLAPPEDWINDOW, R.left, R.top,
		width, height,
		0, 0, GetModuleHandle(NULL),
		NULL);														// Create main window
	while (GetMessage(&Msg, 0, 0, 0)) {								// Get messages
		TranslateMessage(&Msg);										// Translate each message
		DispatchMessage(&Msg);										// Dispatch each message
	}


    delete [] zbuffer;
    return 0;
}

