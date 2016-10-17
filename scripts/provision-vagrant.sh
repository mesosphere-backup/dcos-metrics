#!/bin/bash
# This script provisions a single-node Mesos cluster that can be used for
# developing and testing the dcos-metrics project locally. Chances are good
# that, for full end-to-end testing, you'd be better off with a full DC/OS
# cluster. For more info, see https://dcos.io/install.
#
set -e

if [[ $(id -u) -ne 0 ]]; then
    echo "Please re-run this script as root."
    exit 1
fi

DISTRO=$(lsb_release -is | tr '[:upper:]' '[:lower:]')
CODENAME=$(lsb_release -cs)

function parse_args {
    while [[ $# > 1 ]]; do
        case "$1" in
            --mesos_release)    MESOS_RELEASE="$2"                ; shift  ;;
            --marathon_release) MARATHON_RELEASE="$2"             ; shift  ;;
            --golang_release)   GOLANG_RELEASE="$2"               ; shift  ;;
            --ip_address)       IP_ADDRESS="${2:-127.0.0.1}"      ; shift  ;;
            --*)                echo "Error: invalid option '$1'" ; exit 1 ;;
        esac
        shift
    done
}

function _install_pkg_with_version {
    local name="$1"
    local ver="$2"

    if [[ $ver =~ "latest" ]]; then
        echo "Installing ${name}..."
        apt-get -y install "${name}"
    else
        echo "Installing ${name} version ${ver}..."
        apt-get -y install "${name}=${ver}"
    fi
}

function install_prereqs {
    echo "Updating metadata and installing prerequisites..."
    apt-get -y update
    apt-get -y install apt-transport-https ca-certificates git \
        linux-tools-common linux-tools-generic linux-tools-$(uname -r)
    echo debconf shared/accepted-oracle-license-v1-1 select true | sudo debconf-set-selections
    echo debconf shared/accepted-oracle-license-v1-1 seen true | sudo debconf-set-selections
    apt-get install -y oracle-java8-installer oracle-java8-set-default

}

function configure_repos {
    echo "Installing Mesosphere repository..."

    # Use hkp://keyserver.ubuntu.com:80 to work around corporate firewalls
    # that block the native HKP port of 11371.
    apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv E56151BF
    echo "deb http://repos.mesosphere.io/${DISTRO} ${CODENAME} main" \
        | tee /etc/apt/sources.list.d/mesosphere.list

    echo "Installing Java repo"
    add-apt-repository -y ppa:webupd8team/java
    echo "Refreshing metadata..."
    apt-get -y update
}

function install_zookeeper {
    echo "Installing ZooKeeper..."
    apt-get -y install zookeeperd
    echo "1" > /etc/zookeeper/conf/myid
    service zookeeper restart
}

function install_mesos {
    _install_pkg_with_version mesos $MESOS_RELEASE
}

function install_marathon {
    echo "Installing Marathon ..."
    _install_pkg_with_version marathon $MARATHON_RELEASE
    service marathon restart
}

function configure_mesos {
    mkdir -p /etc/{mesos,mesos-master,mesos-slave}

    # Master
    echo "zk://${IP_ADDRESS}:2181/mesos" > /etc/mesos/zk
    echo "${IP_ADDRESS}"                 > /etc/mesos-master/hostname
    echo "${IP_ADDRESS}"                 > /etc/mesos-master/ip
    service mesos-master restart

    # Agent
    # Note: there is a known bug in Mesos when using the 'cgroups/perf_event'
    # isolatoron specific kernels and platforms. For more info, see MESOS-4705.
    echo "1secs"                                                 > /etc/mesos-slave/container_disk_watch_interval
    echo "mesos"                                                 > /etc/mesos-slave/containerizers
    echo "${IP_ADDRESS}"                                         > /etc/mesos-slave/hostname
    echo "${IP_ADDRESS}"                                         > /etc/mesos-slave/ip
    echo "cgroups/cpu,cgroups/mem,cgroups/perf_event,posix/disk" > /etc/mesos-slave/isolation
    echo "cpu-clock,task-clock,context-switches"                 > /etc/mesos-slave/perf_events
    echo "/var/lib/mesos"                                        > /etc/mesos-slave/work_dir
    service mesos-slave restart

    cat << END
--------------------------------------------------
Mesos version ${MESOS_RELEASE} has been installed.

  * Master: http://${IP_ADDRESS}:5050
  * Agent: http://${IP_ADDRESS}:5051

--------------------------------------------------
END
}

function install_golang {
    local GOROOT="/usr/local"

    if [[ -d "${GOROOT}/go" ]]; then
        echo "Found an existing Go installation at ${GOROOT}/go. Skipping install..."
        return
    fi

    echo "Installing Go ${GOLANG_RELEASE}..."
    local GOLANG_URL="https://storage.googleapis.com/golang"
    local GOLANG_FILENAME="go${GOLANG_RELEASE}.linux-amd64.tar.gz"

    local GOPATH="/home/vagrant/work"

    curl -sLO "${GOLANG_URL}/${GOLANG_FILENAME}"
    tar zxf $GOLANG_FILENAME -C $GOROOT

    echo "export GOPATH=${GOPATH}"                              >> /etc/profile
    echo "export PATH=\${PATH}:\${GOPATH}/bin:${GOROOT}/go/bin" >> /etc/profile
    echo "export PATH=\${PATH}:\${GOPATH}/bin"                  >> /etc/profile

    mkdir -p "${GOPATH}/src/github.com/dcos" && chown -R vagrant:vagrant $GOPATH
    ln -fs /vagrant "${GOPATH}/src/github.com/dcos/dcos-metrics"

    # Bring in govendor so we don't have to do this manually each time
    . /etc/profile
    go get github.com/kardianos/govendor

    cat << END
--------------------------------------------------------------
Go ${GOLANG_RELEASE} has been installed to /usr/local/go.
/usr/local/go/bin has been appended to \$PATH in /etc/profile.
/home/vagrant/work has been set as the \$GOPATH.

For more information on getting started with Go, see
https://golang.org/doc/code.html

--------------------------------------------------------------
END
}

function main {
    parse_args "$@"
    configure_repos
    install_prereqs

    install_zookeeper
    install_mesos
    configure_mesos

    install_marathon
    install_golang
}

main "$@"
