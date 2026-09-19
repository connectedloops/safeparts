# THROWAWAY: synthetic-only Linux probe, not a product image.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    g++ pkg-config qt6-base-dev libcups2-dev cups-daemon cups-client \
    fonts-dejavu-core poppler-utils strace python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m probe
COPY qt-memory-printing-prototype.cpp /opt/probe.cpp
RUN g++ -std=c++17 -Wall -Wextra -fPIC /opt/probe.cpp -o /opt/probe \
    $(pkg-config --cflags --libs Qt6Widgets Qt6PrintSupport) -lcups
COPY qt-memory-printing-prototype.container.sh /opt/run-probe.sh
ENTRYPOINT ["bash", "/opt/run-probe.sh"]
