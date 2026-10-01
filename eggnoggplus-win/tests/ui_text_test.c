#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../ui_text.h"

#include "hub_preview.h"
int main(int argc, char** argv) {
    WNDCLASSA cls = {0};
    PIXELFORMATDESCRIPTOR format = {0};
    HWND window; HDC dc; HGLRC context;
    unsigned char* pixels;
    BITMAPFILEHEADER file_header = {0}; BITMAPINFOHEADER bitmap_header = {0};
    int width = argc > 2 ? atoi(argv[2]) : 960;
    int height = width * 9 / 16;
    GLint matrix_mode, binding, env_mode;
    GLfloat projection[16], restored[16];
    FILE* output;
    assert(argc >= 2 && width >= 960);
    cls.lpfnWndProc = DefWindowProcA; cls.lpszClassName = "YuleUiTextTest";
    cls.hInstance = GetModuleHandleA(NULL); cls.style = CS_OWNDC;
    assert(RegisterClassA(&cls));
    window = CreateWindowA(cls.lpszClassName, "UI font test", WS_POPUP,
                            0, 0, width, height, NULL, NULL, cls.hInstance, NULL);
    assert(window); /* Hidden throughout; this runner never launches the game. */
    dc = GetDC(window); assert(dc);
    format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 24;
    assert(SetPixelFormat(dc, ChoosePixelFormat(dc, &format), &format));
    context = wglCreateContext(dc); assert(context && wglMakeCurrent(dc, context));
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.012f, 0.018f, 0.032f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
    if (argc>3 && atoi(argv[3])>=5) preview_online_state(width,height,atoi(argv[3]));
    else if (argc>3 && atoi(argv[3])>=3) preview_custom(width,height,atoi(argv[3]));
    else preview_hub(width,height,argc>3 ? atoi(argv[3]) : 0);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &matrix_mode); assert(matrix_mode == 7);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &matrix_mode); assert(matrix_mode == 2);
    assert(ui_text_width("WWW",1) > ui_text_width("iii",1));
    assert(ui_text_style_width("WWW",1,UI_TEXT_MONO) ==
           ui_text_style_width("iii",1,UI_TEXT_MONO));
    assert(fabsf(ui_text_width("Online",1.5f) - ui_text_width("Online",1) * 1.5f) < .001f);
    assert(ui_text_scale(1.5f) == 1.5f);
    assert(ui_text_width("I",1) < UI_TEXT_ADVANCE);
    assert(ui_text_style_width("iii",1,UI_TEXT_MONO) == 3 * UI_TEXT_ADVANCE);
    /* The custom UI cannot leak GL state into the game's next draw. */
    glMatrixMode(GL_TEXTURE);
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &env_mode);
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    ui_text_flush((float)width, (float)height);
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode); assert(matrix_mode == GL_TEXTURE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &matrix_mode); assert(matrix_mode == binding);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &matrix_mode); assert(matrix_mode == env_mode);
    glGetFloatv(GL_PROJECTION_MATRIX, restored);
    for (int i=0; i<16; i++) assert(restored[i] == projection[i]);
    assert(glGetError() == GL_NO_ERROR);
    pixels = (unsigned char*)malloc((size_t)width * height * 4); assert(pixels);
    glReadPixels(0,0,width,height,GL_BGRA_EXT,GL_UNSIGNED_BYTE,pixels);
    bitmap_header.biSize = sizeof(bitmap_header); bitmap_header.biWidth = width;
    bitmap_header.biHeight = height; bitmap_header.biPlanes = 1; bitmap_header.biBitCount = 32;
    file_header.bfType = 0x4d42; file_header.bfOffBits = sizeof(file_header) + sizeof(bitmap_header);
    file_header.bfSize = file_header.bfOffBits + width*height*4;
    output = fopen(argv[1],"wb"); assert(output);
    assert(fwrite(&file_header,sizeof(file_header),1,output)==1);
    assert(fwrite(&bitmap_header,sizeof(bitmap_header),1,output)==1);
    assert(fwrite(pixels, (size_t)width*height*4,1,output)==1); fclose(output); free(pixels);
    ui_text_shutdown(); wglMakeCurrent(NULL,NULL); wglDeleteContext(context);
    ReleaseDC(window,dc); DestroyWindow(window);
    puts("Custom UI font, alignment and OpenGL state restoration: PASS");
    return 0;
}
