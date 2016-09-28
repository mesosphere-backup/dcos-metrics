# Contributing
Although this project has dedicated engineers from Mesosphere working on modifications,
community contributions are essential for keeping dcos-metrics great. In order to keep
it as easy as possible for people to contribute, we have a few guidelines that we'd
like contributors to follow.

If you have any questions on the below guidelines, please feel free to contact us by using
any of the community resources listed in this project's [README.md][dcos-metrics-readme] file.

## Getting Started
  * Make sure you have a [GitHub][github-join] account.
  * Submit a ticket for your issue in the [DC/OS JIRA][dcos-jira], using the component
  `dcos-metrics`.
    * Clearly describe the issue and any steps you need to reproduce it.
    * Be sure to include the version or SHA of the software you're using, and any
    other information about your environment that might be relevant.
  * Fork the repository on GitHub.

## Making Changes
  * Create a topic branch from where you want to base your work. Topic branches should typically
  be based on the `master` branch.
    * To quickly create a topic branch based on master, run `git checkout -b my-patch master`.
    * Except for trivial changes, please avoid working directly on the `master` branch.
  * Make commits of logical units. Ideally, commits could be tested and applied individually.

### Trivial changes
For trivial changes and documentation updates, it's not always necessary to create a new ticket
or topic branch. Please, use your best judgement here.

### Style guidelines
Before submitting your pull request, please consider the following:
  * Check for unnecessary whitespace by running `git diff --check`.
  * Check for syntax and style issues:
    * For Go code — by running `go fmt` and using our preferred linter [Golint][golint-github].
    * For C++ code — see the [Google C++ Style Guide][google-cpp-style].
    * For Java code — see the [Google Java Style Guide][google-java-style].
  * Include comments and examples where appropriate.
  * No additional copyright statements or licenses.

We also prefer commit messages to be in the following format:
```
Make the example in CONTRIBUTING imperative and concrete

Without this patch applied, the contributor is left to
imagine what the commit message should look like based
on a description rather than an example.

The first line is a real-life statement, potentially with
a ticket number from the issue tracker. The body describes
the behavior without the patch, why it's a problem, and
how the patch fixes the problem when applied.
```

If you're new to Go, you might also find the [Effective Go][effective-go] documentation helpful.

## Submitting Changes
  * Push your changes to a topic branch in your fork of the repository.
  * Submit a pull request to the main GitHub repository, [dcos/dcos-metrics][dcos-metrics-github].
  * Update any outstanding tickets (if applicable) to mention that you've submitted a change,
  and provide a link to the pull request.

[dcos-jira]: https://dcosjira.atlassian.net
[dcos-metrics-github]: https://github.com/dcos/dcos-metrics
[dcos-metrics-readme]: README.md
[effective-go]: https://golang.org/doc/effective_go.html
[github-join]: https://github.com/join
[golint-github]: https://github.com/golang/lint
[google-cpp-style]: https://google.github.io/styleguide/cppguide.html
[google-java-style]: https://google.github.io/styleguide/javaguide.html