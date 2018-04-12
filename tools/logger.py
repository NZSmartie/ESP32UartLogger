#!/usr/bin/env python3

from datetime import datetime

from flask import Flask, request


def format_date():
    return datetime.now().strftime('%Y-%m-%d')


def format_ts():
    return datetime.now().strftime('%Y-%m-%dT%H.%M.%S')


app = Flask(__name__)


@app.route("/", methods=['POST'])
def hello():
    if not request.args.get('token'):
        return 404, ''
    data = request.get_data(as_text=True)
    with open('logfile.%s.txt' & format_date(), 'a') as file:
        for line in data.splitlines():
            file.write('%s  |%s\n' % (format_ts(), line.strip()))
        file.flush()
    return 201, ""


if __name__ == '__main__':
    app.run('0.0.0.0', 8000, debug=False)