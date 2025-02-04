#ifndef ENV_WIN32_H
#define ENV_WIN32_H


// used by DesktopDCPtr, calls ReleaseDC(0, dc) as the deleter
//
struct DesktopDCReleaser
{
  using pointer = HDC;

  void operator()(HDC dc)
  {
    if (dc != 0) {
      ::ReleaseDC(0, dc);
    }
  }
};

using DesktopDCPtr = std::unique_ptr<HDC, DesktopDCReleaser>;

// used by HMenuPtr, calls DestroyMenu() as the deleter
//
struct HMenuFreer
{
  using pointer = HMENU;

  void operator()(HMENU h)
  {
    if (h != 0) {
      ::DestroyMenu(h);
    }
  }
};

using HMenuPtr = std::unique_ptr<HMENU, HMenuFreer>;

// used by LibraryPtr, calls FreeLibrary as the deleter
//
struct LibraryFreer
{
  using pointer = HINSTANCE;

  void operator()(HINSTANCE h)
  {
    if (h != 0) {
      ::FreeLibrary(h);
    }
  }
};

using LibraryPtr = std::unique_ptr<HINSTANCE, LibraryFreer>;

// used by COMPtr, calls Release() as the deleter
//
struct COMReleaser
{
  void operator()(IUnknown* p)
  {
    if (p) {
      p->Release();
    }
  }
};

template <class T>
using COMPtr = std::unique_ptr<T, COMReleaser>;

// used by MallocPtr, calls std::free() as the deleter
//
struct MallocFreer
{
  void operator()(void* p) { std::free(p); }
};

template <class T>
using MallocPtr = std::unique_ptr<T, MallocFreer>;

// used by LocalPtr, calls LocalFree() as the deleter
//
template <class T>
struct LocalFreer
{
  using pointer = T;

  void operator()(T p) { ::LocalFree(p); }
};

template <class T>
using LocalPtr = std::unique_ptr<T, LocalFreer<T>>;

// used by the CoTaskMemPtr, calls CoTaskMemFree() as the deleter
//
template <class T>
struct CoTaskMemFreer
{
  using pointer = T;

  void operator()(T p) { ::CoTaskMemFree(p); }
};

template <class T>
using CoTaskMemPtr = std::unique_ptr<T, CoTaskMemFreer<T>>;


#endif //ENV_WIN32_H
