# The dependencies of the user scripts the demo runs: torch, openai/CLIP and
# sentence-transformers on top of the released image, which already ships sist2-python.
# One image for both scripts, so torch is downloaded and stored once. Built by
# update_demo.sh, and only rebuilt when the sist2 image changes.

ARG SIST2_IMAGE
FROM ${SIST2_IMAGE}

# torch and torchvision must come from the same index, or torchvision fails to register
# its operators against the torch it was not built for. CLIP is installed without its
# dependencies for that reason, so they are all listed here
RUN pip install --no-cache --break-system-packages \
        --extra-index-url https://download.pytorch.org/whl/cpu \
        torch torchvision ftfy regex tqdm typer packaging sentence-transformers \
    && pip install --no-cache --break-system-packages --no-deps \
        git+https://github.com/openai/CLIP.git

RUN git clone --depth 1 https://github.com/sist2app/sist2-script-clip /opt/sist2-script-clip \
    && git clone --depth 1 https://github.com/sist2app/sist2-script-sbert /opt/sist2-script-sbert

# Bake the models in, so a rebuild does not download them again
RUN python -c "import clip; clip.load('ViT-B/32', device='cpu')" \
    && python -c "from sentence_transformers import SentenceTransformer; \
                  SentenceTransformer('sentence-transformers/all-MiniLM-L6-v2', device='cpu')"
