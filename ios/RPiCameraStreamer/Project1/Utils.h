//
//  Utils.h
//  RPiCameraStreamer
//
//  Created by Gregory Dymarek on 21/12/2014.
//
//

#ifndef RPiCameraStreamer_Utils_h
#define RPiCameraStreamer_Utils_h


@interface Utils : NSObject

- (NSString *)getIPAddress:(BOOL)preferIPv4;
- (NSDictionary *)getIPAddresses;


@end

#endif
