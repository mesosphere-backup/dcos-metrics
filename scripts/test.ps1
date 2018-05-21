$COMPONENT = $args[0]
$TESTSUITE = $args[1]
$SUBDIRS = ""
$PACKAGES = ""
$SOURCE_DIR = ""
$BUILD_DIR = ""
$SOURCE_FILES = ""


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
function fastfailpipe {
  Process  {
  fastfail("hello")
  $_
  }
}

function _gofmt()
{
    logmsg("Running 'gofmt' ...")

    $text = & gofmt -d -l $SOURCE_FILES
    if ($LASTEXITCODE -ne 0)
    {
        Write-Output($text)
        exit -1
    }
}
function _goimports()
{
    logmsg("Running 'goimports' ...")
    & go get -u golang.org/x/tools/cmd/goimports
    fastfail("failed to get goimports")

    $text = & goimports -d -l $SOURCE_FILES
    if ($LASTEXITCODE -ne 0)
    {
        Write-Output($text)
        exit -1
    }
}

function _golint()
{
    logmsg("Running 'golint' ...")
    & go get -u github.com/golang/lint/
    
    $text = & golint -set_exit_status  $PACKAGES
    fastfail("failed to run golint: $text")

    if ($text.length -ne 0)
    {
        Write-Output($text)
        exit -1
    }
}

function _govet()
{
    logmsg("Running 'go vet' ...")

    $text = & go vet $PACKAGES
    fastfail("failed to run go vet $_")
    
    if ($text.length -ne 0)
    {
        Write-Output($text)
        exit -1
    }
}


function _unittest_with_coverage
{
    logmsg "Running unit tests ..."
    $covermode = "count"
    $numFailures = 0

    &go get -u github.com/jstemmer/go-junit-report
    fastfail("failed to: go get -u github.com/jstemmer/go-junit-report")

    &go get -u github.com/smartystreets/goconvey
    fastfail("failed to: go get -u github.com/smartystreets/goconvey")

    &go get -u golang.org/x/tools/cmd/cover
    fastfail("failed to: go get -u golang.org/x/tools/cmd/cover")

    &go get -u github.com/axw/gocov/...
    fastfail("failed to: go get -u github.com/axw/gocov/...")

    &go get -u github.com/AlekSi/gocov-xml
    fastfail("failed to: go get -u github.com/AlekSi/gocov-xml")

    $profilecovfile = ($BUILD_DIR + "/coverage-reports/profile.cov")
    $coveragexmlfile = ($BUILD_DIR + "/coverage-reports/coverage.xml")

    New-Item -ItemType Directory -path ($BUILD_DIR + "/test-reports") -force | Out-Null
    New-Item -ItemType Directory -path ($BUILD_DIR + "/coverage-reports") -force | Out-Null

    ("mode: " + $covermode) | Out-File -encoding ascii -FilePath $profilecovfile

    $allargs = @("list", "-f={{.ImportPath}}", "./...")
    $unittest_packages = & go $allargs
    fastfail("Failed to get package list: $unittest_packages")

    foreach ($import_path in $unittest_packages)
    {
        $package = Split-Path -Leaf $import_path
        Write-Output "Running tests for $import_path"

        if ($import_path -eq 'github.com/dcos/dcos-metrics/vendor/github.com/coreos/go-systemd/activation') {
            Write-Host("Skipped $import_path")
            continue
        }
        $covfile = ($BUILD_DIR + "/coverage-reports/profile_" + $package + ".cov")
        $repfile = ($BUILD_DIR + "/test-reports/" + $package + "-report.xml")

        $allargs = @( "test", "-v", "-tags=`"$TESTSUITE`"", "-covermode=$covermode", "-coverprofile=$covfile", $import_path )
        Write-Host("Calling: & go $allargs")
        $testoutput = & go $allargs
        $testoutput | Out-Default
        if ($LASTEXITCODE -ne 0) 
        {
            Write-Host ("Failed to test $import_path")
            $numFailures = ($numFailures + 1)
        }
#            fastfail("Unittests failed")
        $testoutput | &go-junit-report.exe | Out-File -Encoding ascii $repfile
    }

    Get-ChildItem -File ($BUILD_DIR + "/coverage-reports/") -Filter "profile_*.cov" |
    foreach-object  {
        Get-Content $_.FullName | select -Skip 1 | Out-File -encoding ascii -Append -FilePath $profilecovfile
        Remove-Item -Path $_.FullName
    }

    &go tool cover -func $profilecovfile
    fastfail("Failed to display coverage profile info for functions")

    &gocov convert $profilecovfile| &gocov-xml | Out-File -Encoding ascii -FilePath $coveragexmlfile
    fastfail ("Failed to convert coverage information to $coveragexmlfile")

    if ($numFailures -ne 0)
    {
        Write-Host ("$numFailures unittest packages failed during this run")
        exit -1
    }
}


function main
{
    $SOURCE_DIR = & git rev-parse --show-toplevel
    fastfail("Failed to get SOURCE_DIR")

    $BUILD_DIR = "$SOURCE_DIR/build/$COMPONENT"

    $SOURCE_FILES =  Get-ChildItem -File $SOURCE_DIR -Filter "*.go" -Recurse | ? { $_.FullName -inotmatch "vendor\\"  } | foreach-object  { $_.FullName }

    $PACKAGES = & go list "./..."  | select-string -NotMatch "/schema"

    $test_dirs=".\\"
    $package_dirs="./..."
    $ignore_dirs="schema/"
    $ignore_packages="metricsSchema"


    if ($TESTSUITE -eq "unit")
    {
       # _gofmt
       # _goimports
       # _golint
#go vet disabled currently in linux due to failures
#        _govet

        _unittest_with_coverage
    }
    else
    {
        logmsg("Unsupported test suite $TESTSUITE")
        exit 1
    }

}


if ($args.Count -ne 2)
{
    Write-Output ("Usage: powershell.exe -file test.ps1 component unit")
    Write-Output ("for example: powershell.exe -file test.ps1 collector unit")
    exit -1
}



main
