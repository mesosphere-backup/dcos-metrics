function logmsg($msg)
{
    Write-Output("")
    Write-Output("*** " + $msg + " ***")
}

function fastfail($msg)
{
    if ($LASTEXITCODE -ne 0)
    {
        logmsg($msg)
        exit -1
    }
}

function license_check
{
    $retval=0
    Get-ChildItem -File -Filter *.go -Recurse | ?{ $_.FullName -notmatch "\\vendor\\" } | ?{ $_.FullName -notmatch "\\schema\\" } | ?{ $_.FullName -notmatch "\\examples\\" } |
    ForEach-Object {
        $fullName = $_.FullName
        $output = Select-String -Pattern "Copyright [0-9]{4} Mesosphere, Inc." $fullName
        if (-not $output)
        {
            Write-Output("$fullName does not have proper Mesosphere copyright")
            $retval=$retval+1
        }
        $output = Select-String -SimpleMatch "Licensed under the Apache License, Version 2.0 (the `"License`");" $fullName
        if (-not $output)
        {
            Write-Output("$fullName does not have proper Apache license")
            $retval=$retval+1
        }
    }
}

function build_collector()
{
    & go generate github.com/dcos/dcos-metrics/
    fastfail("Failed to generate schema.go.")

    $filenames = ""
    Get-ChildItem -file -Filter *.go |
    ForEach-Object {
        $filenames = ($filenames + " " + $_.Name)
    }

    Invoke-Expression("go build -a -o $BUILD_DIR/dcos-metrics-$COMPONENT-$GIT_REF.exe -ldflags `"-X main.VERSION=$VERSION -X main.REVISION=$REVISION -X github.com/dcos/dcos-metrics/util/http/client.USERAGENT=dcos-metrics/$VERSION`" $filenames")
    fastfail("Failed to build collector")

}

function build_statsd-emitter()
{
    & go build -a -o "$BUILD_DIR/dcos-metrics-$COMPONENT-$GIT_REF.exe" examples/statsd-emitter/main.go
    fastfail("Failed to build statsd-emitter")
}

function build_plugins()
{
    Get-ChildItem -Directory "$SOURCE_DIR/plugins/"  |
    foreach-object  {
        $pluginName = $_.BaseName
        $pluginPath="$BUILD_DIR/dcos-metrics-" + $pluginName + "-plugin@$GIT_REF.exe"
        Write-Output("Building plugin: " + $pluginName)

        $filenames = ""
        Get-ChildItem -file -Filter "plugins/$pluginName/*.go" |
        ForEach-Object {
            $filenames = ($filenames + " plugins/$pluginName/" + $_.Name)
        }

        Invoke-Expression("go build -a -o $pluginPath -ldflags `"-X github.com/dcos/dcos-metrics/plugins.VERSION=${VERSION}`" $filenames")
        fastfail("Failed to build plugin $pluginName")
    }
}

function main($param)
{
    $COMPONENT=$param
    Write-Output("Building: $COMPONENT")
    $BUILD_DIR="$SOURCE_DIR/build/$COMPONENT"

    Invoke-Expression("build_$COMPONENT")
}


# Main entry point for script
# invocation is generally with one of three parameters for the component:
#   collector
#   statsd-emitter
#   plugins
# This script supports adding all 3.

license_check

if ($args.Count -eq 0)
{
    Write-Output ("Script requires one or all of the parameters 'collector', 'statsd-emitter', and/or 'plugins'")
    exit -1
}

Remove-Item .\build -Force -Recurse -ErrorAction Ignore

foreach ($arg in $args)
{
    $SOURCE_DIR = & git rev-parse --show-toplevel
    fastfail("Failed to find top-level source directory. Cannot find git.exe?")

    $GIT_REF = & git describe --tags --always
    fastfail("Failed to get git ref")

    $VERSION = $GIT_REF

    $REVISION = & git rev-parse --short HEAD
    fastfail("Failed to get revision")

    $PLATFORM = "Windows"

    try
    {
        main $arg
    }
    catch
    {
        Write-Output ("Failed build for component $arg")
        exit -1
    }
}
& tree /F build

