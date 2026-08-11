FROM sist2app/sist2-build as build

WORKDIR /build/

COPY scripts scripts
COPY schema schema
COPY CMakeLists.txt .
COPY third-party third-party
COPY src src
COPY sist2-vue sist2-vue
COPY sist2-admin sist2-admin

RUN cd sist2-vue/ && npm install && npm run build
RUN cd sist2-admin/ && npm install && npm run build

RUN mkdir build && cd build && cmake -DSIST_PLATFORM=x64_linux_docker -DSIST_DEBUG_INFO=on -DSIST_DEBUG=off -DBUILD_TESTS=off -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake ..
RUN cd build && make -j$(nproc)
RUN strip build/sist2 || mv build/sist2_debug build/sist2

FROM --platform="linux/amd64" ubuntu@sha256:965fbcae990b0467ed5657caceaec165018ef44a4d2d46c7cdea80a9dff0d1ea

ENV LANG C.UTF-8
ENV LC_ALL C.UTF-8
ENV SIST2_BINARY /root/sist2
ENV DATA_FOLDER /sist2-admin/

ENTRYPOINT ["/root/sist2"]

RUN apt update && DEBIAN_FRONTEND=noninteractive apt install -y curl libasan5 libmagic1 python3  \
    python3-pip git tesseract-ocr && \
    curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && \
    DEBIAN_FRONTEND=noninteractive apt install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir -p /usr/share/tessdata && \
    cd /usr/share/tessdata/ && \
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/hin.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/hin.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/jpn.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/jpn.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/eng.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/eng.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/fra.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/fra.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/rus.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/rus.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/osd.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/osd.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/spa.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/spa.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/deu.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/deu.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/equ.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/equ.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/pol.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/pol.traineddata &&\
    curl -o /usr/share/tesseract-ocr/4.00/tessdata/chi_sim.traineddata https://raw.githubusercontent.com/tesseract-ocr/tessdata/master/chi_sim.traineddata

# sist2
COPY --from=build /build/build/sist2 /root/sist2

# sist2-admin (Node.js; python is kept for user scripts only)
RUN ln /usr/bin/python3 /usr/bin/python
RUN python -m pip install --no-cache git+https://github.com/sist2app/sist2-python.git@2.1
COPY --from=build /build/sist2-admin/server/ /root/sist2-admin/server/
COPY --from=build /build/sist2-admin/frontend/dist/ /root/sist2-admin/frontend/dist/
