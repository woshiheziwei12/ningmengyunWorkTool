# Microsoft Edge WebView2 SDK

Pinned SDK: Microsoft.Web.WebView2 **1.0.3650.58**. Only the native header and x86 loader are vendored; the Evergreen Runtime is installed separately. The build copies `WebView2Loader.dll` next to the application. No browser runtime is bundled.

Source: https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3650.58/microsoft.web.webview2.1.0.3650.58.nupkg

Package SHA-256: `911A472128C82AC8BAA0C486C23342CC9DD6E7DC50D754E676726642CA065C60`

See LICENSE.txt and NOTICE.txt for redistribution terms. The files copied from the package are `build/native/include/WebView2.h` and `build/native/x86/WebView2Loader.dll`.

Integration reference: https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/win32
