
function run_docker {
	docker run --net=host --rm \
	  -e LIBPROCESS_IP=127.0.0.1 \
	  mesosphere/mesos:1.5.0 \
	  mesos-execute \
	    --master=zk://127.0.0.1:2181/mesos \
	    --name=metrics-test \
	    --docker_image=library/alpine \
	    --command="${1}"
}

function echo_statsd_envs {
	local cmd=$(cat <<- EOF
		echo "STATSD_UDP_HOST: \$STATSD_UDP_HOST"
		echo "STATSD_UDP_PORT: \$STATSD_UDP_PORT"
	EOF
	)
	run_docker ${cmd}
}

function emit_single_metric {
	local cmd=$(cat <<- EOF
		echo "metric:1234|c" | nc \$STATSD_UDP_HOST \$STATSD_UDP_PORT
	EOF
	)
	run_docker ${cmd}
}

echo_statsd_envs
#emit_single_metric
