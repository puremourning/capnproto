# A scratch image holding the static Linux tools at /opt/capnp, for use in other images:
#
#   COPY --from=ghcr.io/<owner>/capnproto-newtype:latest /opt/capnp /opt/capnp
#
# capnp finds its standard schemas and plugins relative to its own location, so the tree can be
# copied anywhere (and bin/capnp symlinked onto $PATH). It can also be run directly:
#
#   docker run --rm -v "$PWD":/w -w /w ghcr.io/<owner>/capnproto-newtype compile -oc++ foo.capnp
#
# The build context contains one unpacked tools tree per architecture, named by $TARGETARCH.

FROM scratch
ARG TARGETARCH
COPY ${TARGETARCH}/ /opt/capnp/
ENTRYPOINT ["/opt/capnp/bin/capnp"]
