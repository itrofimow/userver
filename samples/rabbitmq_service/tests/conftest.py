import json

import pytest


pytest_plugins = [
    'pytest_userver.plugins',
    'pytest_userver.plugins.samples',
    # RabbitMQ related plugin, don't ask why this is 'databases'
    'testsuite.databases.rabbitmq.pytest_plugin',
]


SECDIST_CONFIG = {
    # /// [RabbitMQ service sample - secdist]
    # json
    'rabbitmq_settings': {
        'my-rabbit-alias': {
            'hosts': ['localhost'],
            'port': 8672,
            'login': 'guest',
            'password': 'guest',
            'vhost': '/',
        },
    },
    # /// [RabbitMQ service sample - secdist]
}


@pytest.fixture(scope='session')
def service_env():
    return {'SECDIST_CONFIG': json.dumps(SECDIST_CONFIG)}


@pytest.fixture
def client_deps(rabbitmq):
    pass
