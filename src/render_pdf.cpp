// PDF-Rendering (auch moderne .ai-Dateien) ueber Windows.Data.Pdf (WinRT).
// Die asynchronen WinRT-Aufrufe laufen strikt auf einem Worker-Thread, damit
// der (moeglicherweise STA-) Aufruferthread des Explorers nie blockiert wird.

#include <windows.h>
#include <shcore.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <thread>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include "render_common.h"
#include "renderers.h"

using Microsoft::WRL::ComPtr;

namespace {

// Laeuft auf dem Worker-Thread (MTA): PDF laden, Seite 1 als PNG rendern.
HRESULT RenderPdfWorker(const unsigned char* data, size_t len, UINT cx,
                        std::vector<unsigned char>& pngOut)
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // bereits initialisiert - ok
    }

    HRESULT result = E_FAIL;
    try {
        using namespace winrt::Windows::Data::Pdf;
        using namespace winrt::Windows::Storage::Streams;

        // Speicherstream -> IRandomAccessStream
        ComPtr<IStream> mem(SHCreateMemStream(data, static_cast<UINT>(len)));
        if (!mem)
            return E_OUTOFMEMORY;

        winrt::com_ptr<IRandomAccessStream> ras;
        HRESULT hr = CreateRandomAccessStreamOverStream(
            mem.Get(), BSOS_DEFAULT,
            winrt::guid_of<IRandomAccessStream>(), ras.put_void());
        if (FAILED(hr))
            return hr;
        IRandomAccessStream raStream = ras.as<IRandomAccessStream>();

        PdfDocument doc = PdfDocument::LoadFromStreamAsync(raStream).get();
        if (!doc || doc.PageCount() == 0)
            return E_FAIL;

        PdfPage page = doc.GetPage(0);
        auto size = page.Size();
        if (size.Width <= 0 || size.Height <= 0)
            return E_FAIL;

        UINT dstW, dstH;
        if (size.Width >= size.Height) {
            dstW = cx;
            dstH = static_cast<UINT>(cx * size.Height / size.Width + 0.5f);
        } else {
            dstH = cx;
            dstW = static_cast<UINT>(cx * size.Width / size.Height + 0.5f);
        }
        if (dstW == 0) dstW = 1;
        if (dstH == 0) dstH = 1;

        PdfPageRenderOptions opts;
        opts.DestinationWidth(dstW);
        opts.DestinationHeight(dstH);

        InMemoryRandomAccessStream outStream;
        page.RenderToStreamAsync(outStream, opts).get();

        // Stream -> Bytes
        const UINT64 outSize = outStream.Size();
        if (outSize == 0 || outSize > 256ull * 1024 * 1024)
            return E_FAIL;
        outStream.Seek(0);
        DataReader reader(outStream.GetInputStreamAt(0));
        reader.LoadAsync(static_cast<uint32_t>(outSize)).get();
        pngOut.resize(static_cast<size_t>(outSize));
        reader.ReadBytes(winrt::array_view<uint8_t>(
            pngOut.data(), pngOut.data() + pngOut.size()));
        result = S_OK;
    } catch (const winrt::hresult_error& e) {
        result = e.code();
    } catch (...) {
        result = E_FAIL;
    }
    return result;
}

} // namespace

HRESULT RenderPdfToHBitmap(const unsigned char* data, size_t len, UINT cx,
                           HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    std::vector<unsigned char> png;
    HRESULT hr = E_FAIL;
    std::thread worker([&]() { hr = RenderPdfWorker(data, len, cx, png); });
    worker.join();
    if (FAILED(hr))
        return hr;

    return DecodeImageBytesToHBitmap(png.data(), png.size(), cx, phbmp);
}
