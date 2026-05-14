#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <dispatch/dispatch.h>

#include "../jsc_webview.h"
#include "../jsc_embed.h"
#include "../jsc_runtime.h"

#include <string.h>

@interface JscEmbedSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation JscEmbedSchemeHandler

static NSString *normPath(NSURL *u) {
    if (!u) return @"";
    NSString *p = u.path ?: @"";
    if ([p hasPrefix:@"/"]) p = [p substringFromIndex:1];
    return p;
}

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id <WKURLSchemeTask>)task {
    NSURL *url = task.request.URL;
    NSString *ps = normPath(url);
    const char *cpath = ps.length ? [ps UTF8String] : "";

    if ([ps hasPrefix:@"__bridge__/"]) {
        NSString *tail = [ps substringFromIndex:11];
        NSRange slash = [tail rangeOfString:@"/"];
        NSString *fn = tail;
        NSString *arg = @"";
        if (slash.location != NSNotFound) {
            fn = [tail substringToIndex:slash.location];
            arg = [tail substringFromIndex:slash.location + 1];
        }
        NSString *decodedArg = [arg stringByRemovingPercentEncoding] ?: @"";
        char *out = jsc_hybrid_call_aot_cstr([fn UTF8String], [decodedArg UTF8String]);
        NSString *respStr = out ? [NSString stringWithUTF8String:out] : @"";
        if (out) free(out);
        NSData *body = [respStr dataUsingEncoding:NSUTF8StringEncoding];
        NSURLResponse *resp = [[NSURLResponse alloc]
            initWithURL:url
               MIMEType:@"text/plain; charset=utf-8"
     expectedContentLength:(NSInteger)body.length
          textEncodingName:nil];
        [task didReceiveResponse:resp];
        [task didReceiveData:body];
        [task didFinish];

        NSData *raw = [decodedArg dataUsingEncoding:NSUTF8StringEncoding];
        NSString *b64 =
            [raw base64EncodedStringWithOptions:0];
        NSString *js = [NSString
            stringWithFormat:
                @"window.dispatchEvent(new CustomEvent('alloy-backend',{detail:{"
                @"ok:true,from:'native',payloadB64:'%@'}}));",
                b64 ?: @""];
        dispatch_async(dispatch_get_main_queue(), ^{
            [webView evaluateJavaScript:js completionHandler:nil];
        });
        return;
    }

    const unsigned char *data = nullptr;
    size_t len = 0;
    const char *mime = nullptr;

    NSError *fail = nullptr;
    if (!jsc_embed_lookup(cpath, &data, &len, &mime)) {
        fail = [NSError errorWithDomain:@"alloy.embed" code:404 userInfo:nil];
        [task didFailWithError:fail];
        return;
    }

    NSString *mimeStr = mime ? [NSString stringWithUTF8String:mime]
                             : @"application/octet-stream";
    NSURLResponse *resp =
        [[NSURLResponse alloc] initWithURL:url MIMEType:mimeStr expectedContentLength:(NSInteger)len
                          textEncodingName:nil];
    NSData *nd = [NSData dataWithBytes:data length:(NSUInteger)len];
    [task didReceiveResponse:resp];
    [task didReceiveData:nd];
    [task didFinish];
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id <WKURLSchemeTask>)task {
    (void)webView;
    (void)task;
}

@end

extern "C" void jsc_webview_run_modal(const char *title, const char *html,
                                        int width, int height) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        int w = width > 0 ? width : 800;
        int h = height > 0 ? height : 600;
        NSRect frame = NSMakeRect(100, 100, w, h);

        NSWindow *win = [[NSWindow alloc]
              initWithContentRect:frame
                            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                       NSWindowStyleMaskResizable)
                              backing:NSBackingStoreBuffered defer:NO];
        [win setTitle:[NSString stringWithUTF8String:title ? title : "alloy app"]];

        WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
        [cfg setURLSchemeHandler:[[JscEmbedSchemeHandler alloc] init]
                    forURLScheme:@"alloy"];

        WKWebView *wv =
            [[WKWebView alloc] initWithFrame:[[win contentView] bounds]
                                                configuration:cfg];
        [wv setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        NSURL *base = [NSURL URLWithString:@"alloy://app/"];
        NSString *htmlStr =
            [NSString stringWithUTF8String:html ? html : ""];
        [wv loadHTMLString:htmlStr baseURL:base];

        [[win contentView] addSubview:wv];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
}
