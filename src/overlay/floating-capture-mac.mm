// macOS: saca la ventana flotante de las capturas de pantalla (NSWindowSharingNone).

#import <AppKit/AppKit.h>

void floating_exclude_from_capture(void *nsView)
{
	NSView *view = (__bridge NSView *)nsView;
	[view.window setSharingType:NSWindowSharingNone];
}
