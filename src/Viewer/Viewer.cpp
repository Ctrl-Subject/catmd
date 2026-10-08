#include <GL/glui.h>
#include <GL/freeglut.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#endif

namespace {

int main_window = 0;
GLUI_FileBrowser *file_browser = NULL;
GLUI_TextBox *file_preview = NULL;
struct ViewBlock {
  std::string text;
  GLuint texture;
  int width;
  int height;
  std::vector<unsigned char> png;
  int draw_x;
  int draw_y;
  int draw_width;
  int draw_height;
};
std::vector<ViewBlock> view_blocks;
int view_scroll = 0;
float view_zoom = 1.0f;
int view_pan_x = 0;
bool dragging = false;
int last_mouse_x = 0;
int last_mouse_y = 0;
int hovered_image = -1;
int context_menu = 0;

void clear_view()
{
  for (std::vector<ViewBlock>::const_iterator block = view_blocks.begin();
       block != view_blocks.end(); ++block) {
    if (block->texture)
      glDeleteTextures(1, &block->texture);
  }
  view_blocks.clear();
  view_scroll = 0;
  view_pan_x = 0;
  hovered_image = -1;
}

void append_text_block(const std::string &text)
{
  if (text.empty())
    return;
  ViewBlock block = {};
  block.text = text;
  view_blocks.push_back(block);
}

std::vector<unsigned char> decode_base64(const std::string &encoded)
{
  static const std::string alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<unsigned char> decoded;
  unsigned int accumulator = 0;
  int bits = 0;
  for (std::string::const_iterator ch = encoded.begin(); ch != encoded.end(); ++ch) {
    if (*ch == '=')
      break;
    const std::string::size_type value = alphabet.find(*ch);
    if (value == std::string::npos)
      continue;
    accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xff));
    }
  }
  return decoded;
}

#ifdef _WIN32
GLuint png_texture(const std::vector<unsigned char> &png, int &width, int &height)
{
  IStream *stream = NULL;
  if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK)
    return 0;
  ULONG written = 0;
  stream->Write(&png[0], static_cast<ULONG>(png.size()), &written);
  LARGE_INTEGER start = { 0 };
  stream->Seek(start, STREAM_SEEK_SET, NULL);
  Gdiplus::Bitmap bitmap(stream, FALSE);
  GLuint texture = 0;
  if (bitmap.GetLastStatus() == Gdiplus::Ok) {
    width = static_cast<int>(bitmap.GetWidth());
    height = static_cast<int>(bitmap.GetHeight());
    Gdiplus::Rect bounds(0, 0, width, height);
    Gdiplus::BitmapData data;
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
      std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4);
      for (int row = 0; row < height; ++row) {
        const unsigned char *source = static_cast<const unsigned char *>(data.Scan0) +
          row * data.Stride;
        unsigned char *target = &rgba[static_cast<std::size_t>(row) * width * 4];
        for (int col = 0; col < width; ++col) {
          target[col * 4] = source[col * 4 + 2];
          target[col * 4 + 1] = source[col * 4 + 1];
          target[col * 4 + 2] = source[col * 4];
          target[col * 4 + 3] = source[col * 4 + 3];
        }
      }
      bitmap.UnlockBits(&data);
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    }
  }
  stream->Release();
  return texture;
}

bool copy_png_to_clipboard(const std::vector<unsigned char> &png)
{
  if (png.empty())
    return false;
  IStream *stream = NULL;
  if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK)
    return false;
  ULONG written = 0;
  stream->Write(&png[0], static_cast<ULONG>(png.size()), &written);
  LARGE_INTEGER start = { 0 };
  stream->Seek(start, STREAM_SEEK_SET, NULL);
  Gdiplus::Bitmap bitmap(stream, FALSE);
  bool copied = false;
  if (bitmap.GetLastStatus() == Gdiplus::Ok) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    Gdiplus::Rect bounds(0, 0, width, height);
    Gdiplus::BitmapData data;
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
      const std::size_t pixel_bytes = static_cast<std::size_t>(width) * height * 4;
      const std::size_t dib_bytes = sizeof(BITMAPINFOHEADER) + pixel_bytes;
      HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, dib_bytes);
      if (memory) {
        unsigned char *dib = static_cast<unsigned char *>(GlobalLock(memory));
        if (dib) {
          BITMAPINFOHEADER header = {};
          header.biSize = sizeof(BITMAPINFOHEADER);
          header.biWidth = width;
          header.biHeight = height;
          header.biPlanes = 1;
          header.biBitCount = 32;
          header.biCompression = BI_RGB;
          header.biSizeImage = static_cast<DWORD>(pixel_bytes);
          std::memcpy(dib, &header, sizeof(header));
          unsigned char *pixels = dib + sizeof(header);
          for (int row = 0; row < height; ++row) {
            const unsigned char *source = static_cast<const unsigned char *>(data.Scan0) +
              row * data.Stride;
            unsigned char *target = pixels +
              static_cast<std::size_t>(height - row - 1) * width * 4;
            for (int col = 0; col < width; ++col) {
              target[col * 4] = source[col * 4];
              target[col * 4 + 1] = source[col * 4 + 1];
              target[col * 4 + 2] = source[col * 4 + 2];
              target[col * 4 + 3] = 0xff;
            }
          }
          GlobalUnlock(memory);
          if (OpenClipboard(NULL)) {
            EmptyClipboard();
            copied = SetClipboardData(CF_DIB, memory) != NULL;
            CloseClipboard();
          }
        }
        if (!copied)
          GlobalFree(memory);
      }
      bitmap.UnlockBits(&data);
    }
  }
  stream->Release();
  return copied;
}
#endif

std::vector<unsigned char> run_catmd(const std::string &path)
{
  std::vector<unsigned char> output;
  const char *node = std::getenv("CATMD_NODE");
  const char *cli = std::getenv("CATMD_CLI");
  if (!node || !*node || !cli || !*cli)
    return output;

#ifdef _WIN32
  const auto widen = [](const char *value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, NULL, 0);
    std::wstring result(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
      MultiByteToWideChar(CP_UTF8, 0, value, -1, &result[0], size);
      result.resize(static_cast<std::size_t>(size - 1));
    }
    return result;
  };
  const auto quote = [](const std::wstring &value) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (std::wstring::const_iterator ch = value.begin(); ch != value.end(); ++ch) {
      if (*ch == L'\\') {
        ++slashes;
      } else if (*ch == L'"') {
        result.append(slashes * 2 + 1, L'\\');
        result += *ch;
        slashes = 0;
      } else {
        result.append(slashes, L'\\');
        result += *ch;
        slashes = 0;
      }
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
  };
  std::wstring command = quote(widen(node)) + L" " + quote(widen(cli)) +
    L" --force " + quote(widen(path.c_str()));
  SECURITY_ATTRIBUTES security = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
  HANDLE read_pipe = NULL;
  HANDLE write_pipe = NULL;
  if (!CreatePipe(&read_pipe, &write_pipe, &security, 0))
    return output;
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_pipe;
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process = {};
  const BOOL started = CreateProcessW(NULL, &command[0], NULL, NULL, TRUE,
                                      CREATE_NO_WINDOW, NULL, NULL,
                                      &startup, &process);
  CloseHandle(write_pipe);
  if (!started) {
    CloseHandle(read_pipe);
    return output;
  }
  unsigned char buffer[8192];
  DWORD received = 0;
  while (ReadFile(read_pipe, buffer, sizeof(buffer), &received, NULL) && received)
    output.insert(output.end(), buffer, buffer + received);
  CloseHandle(read_pipe);
  WaitForSingleObject(process.hProcess, INFINITE);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
#else
  const auto quote = [](const std::string &value) {
    std::string result = "'";
    for (std::string::const_iterator ch = value.begin(); ch != value.end(); ++ch)
      result += *ch == '\'' ? "'\\''" : std::string(1, *ch);
    return result + "'";
  };
  const std::string command = quote(node) + " " + quote(cli) + " --force " +
    quote(path) + " 2>/dev/null";
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return output;
  unsigned char buffer[8192];
  std::size_t received = 0;
  while ((received = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
    output.insert(output.end(), buffer, buffer + received);
  pclose(pipe);
#endif
  return output;
}

void append_image_block(const std::string &encoded)
{
  const std::vector<unsigned char> png = decode_base64(encoded);
  if (png.empty())
    return;
#ifdef _WIN32
  int width = 0;
  int height = 0;
  const GLuint texture = png_texture(png, width, height);
  if (texture) {
    ViewBlock block = {};
    block.texture = texture;
    block.width = width;
    block.height = height;
    block.png = png;
    view_blocks.push_back(block);
  } else {
    append_text_block("[Unable to display rendered Mermaid image]\n");
  }
#else
  append_text_block("[Rendered Mermaid image available only in Windows viewer]\n");
#endif
}

void parse_catmd_output(const std::vector<unsigned char> &bytes)
{
  clear_view();
  const std::string output(bytes.begin(), bytes.end());
  std::string text;
  std::size_t position = 0;
  while (position < output.size()) {
    if (output.compare(position, 3, "\x1b_G") == 0) {
      append_text_block(text);
      text.clear();
      std::string encoded;
      bool more = false;
      do {
        const std::size_t separator = output.find(';', position + 3);
        const std::size_t end = output.find("\x1b\\", separator);
        if (separator == std::string::npos || end == std::string::npos) {
          position = output.size();
          break;
        }
        const std::string header = output.substr(position + 3, separator - position - 3);
        encoded.append(output, separator + 1, end - separator - 1);
        more = header.find("m=1") != std::string::npos;
        position = end + 2;
      } while (more && output.compare(position, 3, "\x1b_G") == 0);
      if (!encoded.empty())
        append_image_block(encoded);
      continue;
    }
    if (output.compare(position, 2, "\x1b]") == 0) {
      const std::size_t end = output.find("\x1b\\", position + 2);
      position = end == std::string::npos ? output.size() : end + 2;
      continue;
    }
    if (output.compare(position, 2, "\x1b[") == 0) {
      std::size_t end = position + 2;
      while (end < output.size() &&
             !(static_cast<unsigned char>(output[end]) >= 0x40 &&
               static_cast<unsigned char>(output[end]) <= 0x7e))
        ++end;
      position = end == output.size() ? output.size() : end + 1;
      continue;
    }
    text += output[position++];
  }
  append_text_block(text);
  glutPostRedisplay();
}

void draw_embossed_frame(int left, int top, int width, int height,
                         const char *title)
{
  const int title_top = title && *title ? 4 : 0;

  glLineWidth(1.0f);
  glColor3f(1.0f, 1.0f, 1.0f);
  glBegin(GL_LINE_LOOP);
  glVertex2i(left, top + title_top);
  glVertex2i(left + width, top + title_top);
  glVertex2i(left + width, top + height);
  glVertex2i(left, top + height);
  glEnd();
  glBegin(GL_LINE_LOOP);
  glVertex2i(left + 1, top + title_top + 1);
  glVertex2i(left + width - 1, top + title_top + 1);
  glVertex2i(left + width - 1, top + height - 1);
  glVertex2i(left + 1, top + height - 1);
  glEnd();

  glColor3f(0.5f, 0.5f, 0.5f);
  glBegin(GL_LINE_LOOP);
  glVertex2i(left, top + title_top);
  glVertex2i(left + width - 1, top + title_top);
  glVertex2i(left + width - 1, top + height - 1);
  glVertex2i(left, top + height - 1);
  glEnd();

  if (title && *title) {
    const int title_x = left + 7;
    const int title_width = glutBitmapLength(
      GLUT_BITMAP_HELVETICA_12,
      reinterpret_cast<const unsigned char *>(title));
    glColor3f(0.78f, 0.79f, 0.80f);
    glBegin(GL_QUADS);
    glVertex2i(title_x - 3, top);
    glVertex2i(title_x + title_width + 3, top);
    glVertex2i(title_x + title_width + 3, top + 9);
    glVertex2i(title_x - 3, top + 9);
    glEnd();

    glColor3f(0.28f, 0.28f, 0.28f);
    glRasterPos2i(title_x, top + 4);
    while (*title)
      glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *title++);
  }
}

void draw_inset_frame(int left, int top, int width, int height)
{
  glBegin(GL_LINES);
  glColor3f(0.5f, 0.5f, 0.5f);
  glVertex2i(left, top);
  glVertex2i(left + width, top);
  glVertex2i(left, top);
  glVertex2i(left, top + height);

  glColor3f(1.0f, 1.0f, 1.0f);
  glVertex2i(left, top + height);
  glVertex2i(left + width, top + height);
  glVertex2i(left + width, top + height);
  glVertex2i(left + width, top);

  glColor3f(0.0f, 0.0f, 0.0f);
  glVertex2i(left + 1, top + 1);
  glVertex2i(left + width - 1, top + 1);
  glVertex2i(left + 1, top + 1);
  glVertex2i(left + 1, top + height - 1);

  glColor3f(0.75f, 0.75f, 0.75f);
  glVertex2i(left + 1, top + height - 1);
  glVertex2i(left + width - 1, top + height - 1);
  glVertex2i(left + width - 1, top + height - 1);
  glVertex2i(left + width - 1, top + 1);
  glEnd();
}

void draw_content(int left, int top, int width, int height,
                  int viewport_x, int viewport_y, int viewport_height)
{
  const int content_left = left + 10;
  const int content_top = top + 10;
  const int content_width = width - 20;
  const int content_height = height - 20;
  glEnable(GL_SCISSOR_TEST);
  glScissor(viewport_x + content_left,
            viewport_y + viewport_height - content_top - content_height,
            content_width, content_height);
  int y = content_top - view_scroll;

  if (view_blocks.empty()) {
    glColor3f(0.35f, 0.35f, 0.35f);
    glPixelZoom(view_zoom, view_zoom);
    glRasterPos2i(content_left, y + static_cast<int>(14 * view_zoom));
    const char *message = "Open a Markdown file to view its catmd output.";
    while (*message)
      glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *message++);
    glPixelZoom(1.0f, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    return;
  }

  for (std::vector<ViewBlock>::iterator block = view_blocks.begin();
       block != view_blocks.end(); ++block) {
    if (block->texture) {
      const float scale = std::min(
        1.0f, std::min(static_cast<float>(content_width) / block->width,
                       static_cast<float>(content_height) / block->height)) * view_zoom;
      const int image_width = static_cast<int>(block->width * scale);
      const int image_height = static_cast<int>(block->height * scale);
      block->draw_x = content_left - view_pan_x;
      block->draw_y = y;
      block->draw_width = image_width;
      block->draw_height = image_height;
      if (y + image_height >= content_top && y < content_top + content_height) {
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, block->texture);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2i(block->draw_x, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(block->draw_x + image_width, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(block->draw_x + image_width, y + image_height);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(block->draw_x, y + image_height);
        glEnd();
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
      }
      y += image_height + 12;
      continue;
    }

    int x = content_left - view_pan_x;
    const int line_height = std::max(1, static_cast<int>(18 * view_zoom));
    const int glyph_height = static_cast<int>(13 * view_zoom);
    glPixelZoom(view_zoom, view_zoom);
    for (std::string::const_iterator ch = block->text.begin(); ch != block->text.end(); ++ch) {
      if (*ch == '\n' || *ch == '\r') {
        if (*ch == '\n') {
          x = content_left - view_pan_x;
          y += line_height;
        }
        continue;
      }
      if (static_cast<unsigned char>(*ch) >= 0x80)
        continue;
      const int char_width = std::max(1, static_cast<int>(
        glutBitmapWidth(GLUT_BITMAP_8_BY_13, static_cast<unsigned char>(*ch)) *
        view_zoom));
      if (x + char_width > content_left + content_width) {
        x = content_left - view_pan_x;
        y += line_height;
      }
      if (y + glyph_height >= content_top && y < content_top + content_height) {
        glColor3f(0.12f, 0.12f, 0.12f);
        glRasterPos2i(x, y + glyph_height);
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, static_cast<unsigned char>(*ch));
      }
      x += char_width;
    }
    glPixelZoom(1.0f, 1.0f);
  }
  glDisable(GL_SCISSOR_TEST);
}

void display()
{
  int viewport_x, viewport_y, viewport_width, viewport_height;
  GLUI_Master.get_viewport_area(&viewport_x, &viewport_y,
                                &viewport_width, &viewport_height);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);

  glClearColor(0.78f, 0.79f, 0.80f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glDisable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, viewport_width, viewport_height, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  const int outer_left = 6;
  const int outer_top = 6;
  const int outer_width = viewport_width - 12;
  const int outer_height = viewport_height - 12;
  draw_embossed_frame(outer_left, outer_top, outer_width, outer_height, "View");

  const int frame_left = viewport_width / 25;
  const int frame_top = viewport_height / 25;
  const int frame_width = viewport_width - 2 * frame_left;
  const int frame_height = viewport_height - 2 * frame_top;
  glColor3f(1.0f, 1.0f, 1.0f);
  glBegin(GL_QUADS);
  glVertex2i(frame_left + 2, frame_top + 2);
  glVertex2i(frame_left + frame_width - 2, frame_top + 2);
  glVertex2i(frame_left + frame_width - 2, frame_top + frame_height - 2);
  glVertex2i(frame_left + 2, frame_top + frame_height - 2);
  glEnd();
  draw_inset_frame(frame_left, frame_top, frame_width, frame_height);
  draw_content(frame_left, frame_top, frame_width, frame_height,
               viewport_x, viewport_y, viewport_height);

  glEnable(GL_DEPTH_TEST);

  glutSwapBuffers();
}

void reshape(int, int)
{
  glutPostRedisplay();
}

void set_view_zoom(float zoom)
{
  const float next_zoom = std::max(0.25f, std::min(4.0f, zoom));
  if (next_zoom == view_zoom)
    return;
  view_scroll = static_cast<int>(view_scroll * next_zoom / view_zoom);
  view_zoom = next_zoom;
}

void zoom_in(int)
{
  set_view_zoom(view_zoom * 1.2f);
  glutSetWindow(main_window);
  glutPostRedisplay();
}

void zoom_out(int)
{
  set_view_zoom(view_zoom / 1.2f);
  glutSetWindow(main_window);
  glutPostRedisplay();
}

void zoom_reset(int)
{
  set_view_zoom(1.0f);
  glutSetWindow(main_window);
  glutPostRedisplay();
}

void keyboard(unsigned char key, int, int)
{
  if (key == 'w' || key == 'W' || key == '[')
    view_scroll = std::max(0, view_scroll - 40);
  else if (key == 's' || key == 'S' || key == ']')
    view_scroll += 40;
  else if (key == '+' || key == '=')
    set_view_zoom(view_zoom * 1.2f);
  else if (key == '-')
    set_view_zoom(view_zoom / 1.2f);
  else if (key == '0')
    set_view_zoom(1.0f);
  else if (key == 27 || key == 'q' || key == 'Q')
    std::exit(0);
  glutPostRedisplay();
}

void keyboard_special(int key, int, int)
{
  if (key == GLUT_KEY_UP || key == GLUT_KEY_PAGE_UP)
    view_scroll = std::max(0, view_scroll - 80);
  else if (key == GLUT_KEY_DOWN || key == GLUT_KEY_PAGE_DOWN)
    view_scroll += 80;
  glutPostRedisplay();
}

void mouse_wheel(int, int direction, int, int)
{
  view_scroll = std::max(0, view_scroll - direction * 60);
  glutPostRedisplay();
}

int image_at(int mouse_x, int mouse_y)
{
  int viewport_x, viewport_y, viewport_width, viewport_height;
  GLUI_Master.get_viewport_area(&viewport_x, &viewport_y,
                                &viewport_width, &viewport_height);
  const int viewport_top = glutGet(GLUT_WINDOW_HEIGHT) - viewport_y - viewport_height;
  const int x = mouse_x - viewport_x;
  const int y = mouse_y - viewport_top;
  const int frame_left = viewport_width / 25;
  const int frame_top = viewport_height / 25;
  const int content_left = frame_left + 10;
  const int content_top = frame_top + 10;
  const int content_right = viewport_width - frame_left - 10;
  const int content_bottom = viewport_height - frame_top - 10;
  if (x < content_left || x >= content_right ||
      y < content_top || y >= content_bottom)
    return -1;

  for (std::size_t i = 0; i < view_blocks.size(); ++i) {
    const ViewBlock &block = view_blocks[i];
    if (block.texture && x >= block.draw_x &&
        x < block.draw_x + block.draw_width &&
        y >= block.draw_y && y < block.draw_y + block.draw_height)
      return static_cast<int>(i);
  }
  return -1;
}

void mouse_button(int button, int state, int x, int y)
{
  if (button != GLUT_LEFT_BUTTON)
    return;
  if (state == GLUT_DOWN) {
    hovered_image = image_at(x, y);
    dragging = hovered_image >= 0;
    last_mouse_x = x;
    last_mouse_y = y;
  } else {
    dragging = false;
  }
}

void mouse_motion(int x, int y)
{
  if (!dragging)
    return;
  view_pan_x -= x - last_mouse_x;
  view_scroll -= y - last_mouse_y;
  last_mouse_x = x;
  last_mouse_y = y;
  glutPostRedisplay();
}

void mouse_passive_motion(int x, int y)
{
  hovered_image = image_at(x, y);
}

enum ContextAction {
  COPY_IMAGE = 1,
  SHOW_INFO = 2,
  EXIT_VIEWER = 3
};

void context_action(int action)
{
  if (action == COPY_IMAGE) {
    if (hovered_image < 0 ||
        static_cast<std::size_t>(hovered_image) >= view_blocks.size()) {
#ifdef _WIN32
      MessageBoxW(NULL, L"Right-click a rendered diagram to copy it.",
                  L"catmd viewer", MB_OK | MB_ICONINFORMATION);
#else
      std::fprintf(stderr, "Right-click a rendered diagram to copy it.\n");
#endif
      return;
    }
#ifdef _WIN32
    if (!copy_png_to_clipboard(view_blocks[hovered_image].png))
      MessageBoxW(NULL, L"Could not copy the diagram to the clipboard.",
                  L"catmd viewer", MB_OK | MB_ICONWARNING);
#else
    std::fprintf(stderr, "Image clipboard export is only available on Windows.\n");
#endif
  } else if (action == SHOW_INFO) {
#ifdef _WIN32
    MessageBoxW(NULL,
      L"catmd viewer - release 1.0.0\n\u00a9 Ellie Dawson 2026",
      L"About catmd viewer", MB_OK | MB_ICONINFORMATION);
#else
    std::fprintf(stderr, "catmd viewer - release 1.0.0\n\xC2\xA9 Ellie Dawson 2026\n");
#endif
  } else if (action == EXIT_VIEWER) {
    std::exit(0);
  }
  glutSetWindow(main_window);
  glutPostRedisplay();
}

void load_file(const std::string &path)
{
  glutSetWindow(main_window);
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
  if (!input) {
    clear_view();
    append_text_block("Could not open the selected file.");
    file_preview->set_text("Could not open the selected file.");
    glutPostRedisplay();
    return;
  }

  const std::vector<unsigned char> rendered = run_catmd(path);
  if (rendered.empty()) {
    clear_view();
    append_text_block("Could not run catmd. Launch this viewer with `catmd viewer`.");
  } else {
    parse_catmd_output(rendered);
  }

  const std::string::size_type preview_limit = 64 * 1024;
  std::string contents;
  char buffer[4096];
  while (input && contents.size() < preview_limit) {
    const std::string::size_type remaining = preview_limit - contents.size();
    const std::streamsize bytes_to_read = static_cast<std::streamsize>(
      remaining < sizeof(buffer) ? remaining : sizeof(buffer));
    input.read(buffer, bytes_to_read);
    contents.append(buffer, static_cast<std::string::size_type>(input.gcount()));
  }

  if (contents.find('\0') != std::string::npos) {
    file_preview->set_text("This file appears to be binary and cannot be previewed as text.");
    return;
  }
  if (input.peek() != std::char_traits<char>::eof())
    contents += "\n\n[Preview truncated at 64 KB]";
  if (contents.empty())
    contents = "[Empty file]";

  file_preview->set_text(contents.c_str());
  glutPostRedisplay();
}

void file_selected(int control)
{
  if (control == 1)
    load_file(file_browser->get_file());
}

}

int main(int argc, char **argv)
{
  const std::string initial_file = argc > 1 ? argv[1] : "";
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
  glutInitWindowPosition(50, 50);
  glutInitWindowSize(900, 600);

  main_window = glutCreateWindow("catmd Viewer");
  glutDisplayFunc(display);
  GLUI_Master.set_glutReshapeFunc(reshape);
  GLUI_Master.set_glutKeyboardFunc(keyboard);
  GLUI_Master.set_glutSpecialFunc(keyboard_special);
  glutMouseWheelFunc(mouse_wheel);

#ifdef _WIN32
  Gdiplus::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL);
#endif

  GLUI *controls = GLUI_Master.create_glui_subwindow(
    main_window, GLUI_SUBWINDOW_RIGHT);
  new GLUI_StaticText(controls, "Wheel/arrows scroll; +/- zoom");
  new GLUI_Button(controls, "Zoom In (+)", 0, zoom_in);
  new GLUI_Button(controls, "Zoom Out (-)", 0, zoom_out);
  new GLUI_Button(controls, "Reset Zoom (0)", 0, zoom_reset);
  file_browser = new GLUI_FileBrowser(controls, "Files", GLUI_PANEL_EMBOSSED,
                                      1, file_selected);
  file_browser->set_w(280);
  file_browser->set_h(240);
  new GLUI_StaticText(controls, "Preview:");
  file_preview = new GLUI_TextBox(controls, true);
  file_preview->set_w(280);
  file_preview->set_h(260);
  file_preview->set_text("Double-click a file to preview it.");
  file_preview->disable();
  glutSetWindow(main_window);
  if (!initial_file.empty())
    load_file(initial_file);

  controls->set_main_gfx_window(main_window);
  glutSetWindow(main_window);
  glutKeyboardFunc(keyboard);
  glutSpecialFunc(keyboard_special);
  glutMouseFunc(mouse_button);
  glutMotionFunc(mouse_motion);
  glutPassiveMotionFunc(mouse_passive_motion);
  glutMouseWheelFunc(mouse_wheel);
  context_menu = glutCreateMenu(context_action);
  glutAddMenuEntry("copy image", COPY_IMAGE);
  glutAddMenuEntry("info", SHOW_INFO);
  glutAddMenuEntry("exit", EXIT_VIEWER);
  glutAttachMenu(GLUT_RIGHT_BUTTON);

  glutMainLoop();
  return 0;
}