//
//  media-reader.hpp
//  librfm-example
//
//  Created by Tambet Ingo on 06/11/15.
//  Copyright © 2015 litl. All rights reserved.
//

#ifndef media_reader_hpp
#define media_reader_hpp

#include <vector>
#include <apiary.pb.h>

std::vector<rfm::Media> ReadMediaDirectory(const rfm::Source& source,
                                           const char* directory);

#endif /* media_reader_hpp */
