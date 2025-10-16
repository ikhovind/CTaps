# Use a stable, modern Ubuntu image
FROM ubuntu:22.04

# Set timezone to avoid interactive prompts during package installation
ENV TZ=Etc/UTC
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# Install build dependencies and runtime tools:
# ca-certificates is added here to fix the SSL verification error
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    python3 \
    ca-certificates \
    libglib2.0-dev \
    libuv1-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /app

# Copy the entire project source code into the container
COPY . /app

# Configure the project for building inside the container.
RUN cmake -B /app/build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Set the default command to run when the container starts.
CMD ["/usr/bin/python3", "runtests.py"]
