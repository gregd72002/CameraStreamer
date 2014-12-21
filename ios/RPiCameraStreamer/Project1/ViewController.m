#import "ViewController.h"
#import "GStreamerBackend.h"
#import "Utils.h"
#import <UIKit/UIKit.h>

@interface ViewController () {
    GStreamerBackend *gst_backend;
    Utils *utils;
    int media_width;
    int media_height;
}

@end

@implementation ViewController

/*
 * Methods from UIViewController
 */

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;
    
    /* Make these constant for now, later tutorials will change them */
    media_width = 320;
    media_height = 240;

    
    unsigned char ip[4];
    unsigned int port;
    utils = [[Utils alloc] init];
    NSString *ip_str = [utils getIPAddress:true];
        NSLog(@"IP: %@",ip_str);
    NSArray *fields = [ip_str componentsSeparatedByString:@"."];
    
    ip[0] = [fields[0] intValue];
    ip[1] = [fields[1] intValue];
    ip[2] = [fields[2] intValue];
    ip[3] = [fields[3] intValue];
    port = 8888;
    
    config(ip,port);
    
    gst_backend = [[GStreamerBackend alloc] init:self videoView:video_view];

}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

/* Called when the Play button is pressed */
-(IBAction) play:(id)sender
{
    [gst_backend play];
}

/* Called when the Pause button is pressed */
-(IBAction) pause:(id)sender
{
    [gst_backend pause];
}

- (void)viewDidLayoutSubviews
{
    CGFloat view_width = video_container_view.bounds.size.width;
    CGFloat view_height = video_container_view.bounds.size.height;

    CGFloat correct_height = view_width * media_height / media_width;
    CGFloat correct_width = view_height * media_width / media_height;

    if (correct_height < view_height) {
        video_height_constraint.constant = correct_height;
        video_width_constraint.constant = view_width;
    } else {
        video_width_constraint.constant = correct_width;
        video_height_constraint.constant = view_height;
    }
}

/*
 * Methods from GstreamerBackendDelegate
 */

-(void) gstreamerInitialized
{
    dispatch_async(dispatch_get_main_queue(), ^{
        play_button.enabled = TRUE;
        pause_button.enabled = TRUE;
        message_label.text = @"Ready";
    });
}

-(void) gstreamerSetUIMessage:(NSString *)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
        message_label.text = message;
    });
}

@end
