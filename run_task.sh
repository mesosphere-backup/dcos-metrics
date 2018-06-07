
function run_docker {
	docker run --net=host \
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
	run_docker "${cmd}"
}

function emit_metrics {
	local cmd=$(cat <<- EOF
		for i in \$(seq 1000000); do
			echo "dcos.custom.metric:\$i|c";
			sleep 0.5;
		done | nc -w0 -u \$STATSD_UDP_HOST \$STATSD_UDP_PORT;
	EOF
	)
	run_docker "${cmd}"
}

#echo_statsd_envs
emit_metrics
