# CLIP embeddings for the demo's image corpus: torch and openai/CLIP on top of the
# released image, which already ships sist2-python. Built by update_demo.sh, and
# only rebuilt when the sist2 image changes.

ARG SIST2_IMAGE
FROM ${SIST2_IMAGE}

RUN pip install --no-cache --break-system-packages \
        --extra-index-url https://download.pytorch.org/whl/cpu \
        torch ftfy regex tqdm typer \
    && pip install --no-cache --break-system-packages git+https://github.com/openai/CLIP.git

RUN git clone --depth 1 https://github.com/sist2app/sist2-script-clip /opt/sist2-script-clip
