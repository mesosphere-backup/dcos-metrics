#!/usr/bin/env python3
"""manage-dashboards.py is a small Python tool for getting and creating
metrics dashboards using the Datadog API. For more information, please see

  http://docs.datadoghq.com/api/?lang=python#timeboards
"""
import argparse
import json


try:
    from datadog import initialize, api
except:
    print("""Error: failed to import the 'datadog' module. Is it installed?
You can install the 'datadog' module by running the following command:

    pip install --upgrade datadog

""")
    exit(1)


def create(dash_title, dash_desc, dash_file):
    """Create a dashboard.
    """
    dash_def = {}
    with open(dash_file) as d:
        dash_def = json.load(d)

    try:
        dash_def['dash']
    except KeyError:
        print("Error: {} does not appear to contain a valid Datadog dashboard!"
              .format(dash_file))
        exit(1)

    template_variables = [{}]
    try:
        template_variables = dash_def['dash']['template_variables']
    except KeyError:
        # defaults to empty dict in empty array above
        pass

    print(json.dumps(api.Timeboard.create(
        title=dash_title,
        description=dash_desc,
        graphs=dash_def['dash']['graphs'],
        template_variables=template_variables,
        read_only=False), indent=2))


def get(dash_id):
    """Get a dashboard.
    """
    print(json.dumps(api.Timeboard.get(dash_id), indent=2))


# Main.
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Manage Datadog dashboards.')
    parser.add_argument('--api_key', type=str, help='API key', required=True)
    parser.add_argument('--app_key', type=str, help='App key', required=True)

    subparsers = parser.add_subparsers(dest='action')

    parser_get = subparsers.add_parser('get', help='Get a single dashboard.')
    parser_get.add_argument(
        '--id', type=int, help='The ID of the dashboard.', required=True)

    parser_create = subparsers.add_parser(
        'create', help='Create a single dashboard.')
    parser_create.add_argument(
        '--title', type=str, help='The name of the dashboard.', required=True)
    parser_create.add_argument(
        '--description', type=str, help='The description of the dashboard.',
        required=False)
    parser_create.add_argument(
        '--file', type=str, help='Path to a dashboard in JSON format.',
        required=True)

    args = parser.parse_args()

    options = {
        'api_key': args.api_key,
        'app_key': args.app_key
    }
    initialize(**options)

    if args.action == 'get':
        get(args.id)
    elif args.action == 'create':
        if not args.description:
            args.description = ''
        create(args.title, args.description, args.file)
    else:
        print('Unknown action {}'.format(args.action))
        exit(1)
