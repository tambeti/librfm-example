An example program for librfm.

There's a Xcode project file to help with building and running.

BUILDING

Make sure to update "Header Search Paths" and "Library Search Paths" in
the Build Settings to specify locations of headers and libraries of:
* protobuf
* leveldb
* libcurl
* librfm
* libexif

RUNNING

Edit the Run Scheme (Product -> Scheme -> Edit Scheme...) to add Arguments
to the Run scheme, add a full path to a local directory with some media
files. The example program will upload them all, so it's best to not choose
a directory with too many or too large files.
