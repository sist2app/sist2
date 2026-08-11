# Replacement build for the vcpkg port: same as the fork's CMakeLists,
# plus install rules for the static library and public headers.
cmake_minimum_required(VERSION 3.7)
set(CMAKE_C_STANDARD 11)

project(antiword C)

add_library(antiword STATIC
        src/asc85enc.c src/blocklist.c src/chartrans.c src/datalist.c
        src/depot.c src/dib2eps.c src/doclist.c src/fail.c src/finddata.c
        src/findtext.c src/fmt_text.c src/fontlist.c src/fonts.c src/fonts_u.c
        src/hdrftrlist.c src/imgexam.c src/imgtrans.c src/jpeg2eps.c
        src/listlist.c src/misc.c src/notes.c src/options.c src/out2window.c src/output.c
        src/pdf.c src/pictlist.c src/png2eps.c src/postscript.c src/prop0.c src/prop2.c
        src/prop2.c src/prop6.c src/prop8.c src/properties.c src/propmod.c src/rowlist.c
        src/sectlist.c src/stylelist.c src/stylesheet.c src/summary.c src/tabstop.c
        src/text.c src/unix.c src/utf8.c src/word2text.c src/worddos.c src/wordlib.c
        src/wordmac.c src/wordole.c src/wordwin.c src/xmalloc.c src/xml.c src/sist2_hotfix.c
)

target_compile_options(
        antiword
        PRIVATE
        -Wall
        -pedantic
        -Ofast
        -g
        -DNDEBUG
        -fPIE
        -fPIC
)

install(TARGETS antiword ARCHIVE DESTINATION lib)
install(FILES
        src/antiword.h
        src/debug.h
        src/drawfile.h
        src/fail.h
        src/fontinfo.h
        src/version.h
        src/wordconst.h
        src/wordtypes.h
        DESTINATION include/antiword
)
