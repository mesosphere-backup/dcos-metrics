#!/bin/sh

if [ -z "${MESOS_VERSION}" ]; then
    if [ -z "$1" ]; then
        echo "SYNTAX: $0 <version>"
        echo "Examples:"
        echo "  $0 0.26.0"
        echo "  CORES=1 MESOS_VERSION=0.26.0 $0"
        exit 1
    fi
    MESOS_VERSION=$1
    if [ -z "${MESOS_VERSION}" ]; then
        MESOS_VERSION=$DEFAULT_MESOS_VERSION
    fi
fi

if [ ! -f "mesos-${MESOS_VERSION}.tar.gz" ]; then
    echo "Downloading Mesos ${MESOS_VERSION}"
    wget http://www.apache.org/dist/mesos/${MESOS_VERSION}/mesos-${MESOS_VERSION}.tar.gz
    if [ $? != 0 ]; then
        echo "Failed to download Mesos."
        exit 1
    fi
else
    echo "Found Mesos ${MESOS_VERSION} package"
fi

if [ ! -d "mesos-${MESOS_VERSION}" ]; then
    echo "Unpacking Mesos ${MESOS_VERSION}"
    tar xf mesos-${MESOS_VERSION}.tar.gz
    if [ $? != 0 ]; then
        echo "Failed to unpack Mesos."
        exit 1
    fi
else
    echo "Found unpacked Mesos ${MESOS_VERSION}"
fi

PREREQ_NAG="Have you installed all the build prerequisites for your OS? See http://mesos.apache.org/gettingstarted/"

cd mesos-${MESOS_VERSION}
MESOS_ROOT_DIR=$(pwd)

BOOTSTRAP_CHECK_FILE="$MESOS_ROOT_DIR/get-mesos-bootstrapped"
if [ -f "${BOOTSTRAP_CHECK_FILE}" ]; then
    echo "Already bootstrapped (found ${BOOTSTRAP_CHECK_FILE})"
else
    echo "\n\n###\nBootstrapping...\n###\n\n"
    ./bootstrap
    if [ $? != 0 ]; then
        echo "Failed to bootstrap. ${PREREQ_NAG}"
        exit 1
    fi
    touch ${BOOTSTRAP_CHECK_FILE}
fi

mkdir -p build
cd build

CONFIGURE_CHECK_FILE="$MESOS_ROOT_DIR/get-mesos-configured"
if [ -f "${CONFIGURE_CHECK_FILE}" ]; then
    echo "Already configured (found ${CONFIGURE_CHECK_FILE})"
else
    echo "\n\n###\nConfiguring...\n###\n\n"
    ../configure
    if [ $? != 0 ]; then
        echo "Failed to configure. ${PREREQ_NAG}"
        exit 1
    fi
    touch ${CONFIGURE_CHECK_FILE}
fi

MAKE_CHECK_FILE="$MESOS_ROOT_DIR/get-mesos-made"
if [ -f "${MAKE_CHECK_FILE}" ]; then
    echo "Already made (found ${MAKE_CHECK_FILE})"
else
    if [ -z "${CORES}" ]; then
        CORES=2
        if [ -f "/proc/cpuinfo" ]; then
            CORES=$(cat /proc/cpuinfo | grep processor | wc -l)
        fi
    fi
    echo "\n\n###\nMaking (${CORES} cores)...\n###\n\n"
    make -j${CORES} V=0
    if [ $? != 0 ]; then
        echo "Failed to make. ${PREREQ_NAG}"
        exit 1
    fi
    touch ${MAKE_CHECK_FILE}
fi
