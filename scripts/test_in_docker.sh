docker build . -t tmp

docker run --rm -it\
  -v $(pwd):/host \
  tmp \
  scan --ocr-lang eng --ocr-ebooks -t6 --incremental --very-verbose \
  -o /host/docker.sist2 /host/third-party/libscan-test-files/test_files/