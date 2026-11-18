// CyPort test mod for RedHttpClient (needs RedFileSystem too, to leave a result you can read on the phone).
// Install as r6/scripts/CyPortRedHttpClientTest.reds. Opening the main menu sends two requests (the phone must be online):
//   GET https://example.com/                        -> r6/storages/shared/cyport-http-get.txt   "status=200 OK ..., <!doctype html>..."
//   GET https://self-signed.badssl.com/             -> r6/storages/shared/cyport-http-tls.txt   "no response (the certificate was refused)"
// The second one proves that TLS verification is on: a server with a self-signed certificate must NOT answer.
module CyPortTest.RedHttpClient
import RedHttpClient.*
import RedFileSystem.*

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  let probe = new RedHttpClientTestProbe();
  probe.storage = FileSystem.GetSharedStorage();
  // The callback only holds a weak reference: the delay system keeps the probe alive for a minute.
  GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(probe, 60.0, false);
  AsyncHttpClient.Get(HttpCallback.Create(probe, n"OnPage"), "https://example.com/");
  AsyncHttpClient.Get(HttpCallback.Create(probe, n"OnBadCertificate"), "https://self-signed.badssl.com/");
  return result;
}

public class RedHttpClientTestProbe extends DelayCallback {
  public let storage: ref<FileSystemStorage>;

  public func Call() -> Void {}

  private func Write(name: String, text: String) -> Void {
    FTLog(s"[CyPortTest] RedHttpClient: \(name): \(text)");
    if IsDefined(this.storage) {
      this.storage.GetFile(name).WriteText(text + "\n");
    }
  }

  public func OnPage(response: ref<HttpResponse>) -> Void {
    if !IsDefined(response) {
      this.Write("cyport-http-get.txt", "no response (offline, or the request failed: see red4ext/logs/RedHttpClient*.log)");
      return;
    }
    this.Write("cyport-http-get.txt", s"status=\(response.GetStatusCode()) \(response.GetStatus()) content-type=\(response.GetHeader("Content-Type")) body=\(StrLen(response.GetText())) chars, \(StrLeft(response.GetText(), 60))");
  }

  public func OnBadCertificate(response: ref<HttpResponse>) -> Void {
    if IsDefined(response) {
      this.Write("cyport-http-tls.txt", s"WRONG: a self-signed server answered with status \(response.GetStatusCode())");
    } else {
      this.Write("cyport-http-tls.txt", "no response (the certificate was refused): TLS verification is on");
    }
  }
}
