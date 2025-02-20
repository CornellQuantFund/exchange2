# Stage 1: Build the application
FROM gcc:10 AS builder

# Set the working directory
WORKDIR /app

# Copy CMakeLists.txt and project files
COPY CMakeLists.txt main.cpp src/ include/ /app/

# Create build directory and compile the application
RUN g++ -std=c++17 -o main main.cpp src/*.cpp

# Stage 2: Create the final image
FROM debian:buster-slim

# Install runtime dependencies (Boost)
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    libboost-system1.67.0 \
    libboost-thread1.67.0 \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /app

# Copy the built executable from the builder stage
COPY --from=builder /app/build/main /app/main

# Copy necessary static and template files
COPY static/ /app/static/
COPY templates/ /app/templates/

# Expose the ports your application uses
EXPOSE 8080
EXPOSE 9090

# Define the entry point
CMD ["./main"]
