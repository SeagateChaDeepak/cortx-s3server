import argparse
import re
import yaml
import sys
import os
import imp
import botocore

from boto3.session import Session

def iam_usage():
    return '''
    AssumeRoleWithSAML --saml_principal_arn <SAML IDP ARN> --saml_role_arn <Role ARN> \
    --saml_assertion <File containing SAML Assertion>
        -f <Policy Document> -d <Duration in seconds>
    CreateAccount -n <Account Name> -e <Email Id>
    ListAccounts
    CreateAccessKey
        -n <User Name>
    CreateGroup -n <Group Name>
        -p <Path>
    CreatePolicy -n <Policy Name> -f <Path of the policy document>
        -p <Path> --description <Description of the policy>
    CreateRole -n <Role Name> -f <Path of role policy document>
        -p <Path>
    CreateSAMLProvider -n <Name of saml provider> -f <Path of metadata file>
    CreateUser -n <User Name>
        -p <Path (Optional)
    DeleteAccesskey -k <Access Key Id to be deleted>
        -n <User Name>
    Delete Role -n <Role Name>
    DeleteSamlProvider --arn <Saml Provider ARN>
    DeleteUser -n <User Name>
    GetFederationToken -n <User Name>
        -d <Duration in second> -f <Policiy Document File>
    ListAccessKeys
        -n <User Name>
    ListRoles -p <Path Prefix>
    ListSamlProviders
    ListUsers
        -p <Path Prefix>
    UpdateSAMLProvider --arn <SAML Provider ARN> -f <Path of metadata file>
    UpdateUser -n <Old User Name>
        --new_user <New User Name> -p <New Path>
    UpdateAccessKey -k <access key to be updated> -s <Active/Inactive>
        -n <User Name>
    '''

def get_conf_dir():
    return os.path.join(os.path.dirname(__file__), '../', 'config')

# Import module
def import_module(module_name):
    fp, pathname, description = imp.find_module(module_name) #, sys.path)

    try:
        return imp.load_module(module_name, fp, pathname, description)
    finally:
        if fp:
            fp.close()

# Convert the string to Class Object.
def str_to_class(module, class_name):
    return getattr(module, class_name)

# Create a new IAM serssion.
def get_session(access_key, secret_key, session_token = None):
    return Session(aws_access_key_id=access_key,
                      aws_secret_access_key=secret_key,
                      aws_session_token=session_token)

# Create an IAM client.
def get_client(session, service):
    endpoints_file = os.path.join(get_conf_dir(), 'endpoints.yaml')
    with open(endpoints_file, 'r') as f:
        endpoints = yaml.safe_load(f)

    return session.client(service, use_ssl='false', endpoint_url=endpoints[service])

parser = argparse.ArgumentParser(usage = iam_usage())
parser.add_argument("action", help="Action to be performed.")
parser.add_argument("-n", "--name", help="Name.")
parser.add_argument("-e", "--email", help="Email id.")
parser.add_argument("-p", "--path", help="Path or Path Prefix.")
parser.add_argument("-f", "--file", help="File Path.")
parser.add_argument("-d", "--duration", help="Access Key Duration.", type = int)
parser.add_argument("-k", "--access_key_update", help="Access Key to be updated or deleted.")
parser.add_argument("-s", "--status", help="Active/Inactive")
parser.add_argument("--access_key", help="Access Key Id.")
parser.add_argument("--secret_key", help="Secret Key.")
parser.add_argument("--session_token", help="Session Token.")
parser.add_argument("--arn", help="ARN.")
parser.add_argument("--description", help="Description of the entity.")
parser.add_argument("--saml_principal_arn", help="SAML Principal ARN.")
parser.add_argument("--saml_role_arn", help="SAML Role ARN.")
parser.add_argument("--saml_assertion", help="File conataining SAML assertion.")
parser.add_argument("--new_user", help="New user name.")
cli_args = parser.parse_args()

controller_action_file = os.path.join(get_conf_dir(), 'controller_action.yaml')
with open(controller_action_file, 'r') as f:
    controller_action = yaml.safe_load(f)

"""
Check if the action is valid.
Note - class name and module name are the same
"""
try:
    class_name = controller_action[cli_args.action.lower()]['controller']
except Exception as ex:
    print("Action not found")
    print(str(ex))
    sys.exit()

if(cli_args.action.lower() not in ["createaccount", "listaccounts"] ):
    if(cli_args.access_key is None):
        print("Provide access key")
        sys.exit()

    if(cli_args.secret_key is None):
        print("Provide secret key")
        sys.exit()

if(not 'service' in controller_action[cli_args.action.lower()].keys()):
    print("Set the service(iam/s3/sts) for the action in the controller_action.yml.")
    sys.exit()

service_name = controller_action[cli_args.action.lower()]['service']

# Create boto3.session object using the access key id and the secret key
session = get_session(cli_args.access_key, cli_args.secret_key, cli_args.session_token)

# Create boto3.client object.
client = get_client(session, service_name)

# If module is not specified in the controller_action.yaml, then assume
# class name as the module name.
if('module' in controller_action[cli_args.action.lower()].keys()):
    module_name = controller_action[cli_args.action.lower()]['module']
else:
    module_name = class_name.lower()

try:
    module = import_module(module_name)
except Exception as ex:
    print("Internal error. Module %s not found" % class_name)
    print(str(ex))
    sys.exit()

# Create an object of the controller (user, role etc)
try:
    controller_obj = str_to_class(module, class_name)(client, cli_args)
except Exception as ex:
    print("Internal error. Class %s not found" % class_name)
    print(str(ex))
    sys.exit()

action = controller_action[cli_args.action.lower()]['action']

# Call the method of the controller i.e Create, Delete, Update or List
try:
    getattr(controller_obj, action)()
except Exception as ex:
    print(str(ex))
    sys.exit()