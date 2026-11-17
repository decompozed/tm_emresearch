"""ZAP hook for the account API scan: sends the bearer token of a throwaway account with every request."""
import os


def zap_started(zap, target):
    token = os.environ.get('ZAP_ACCOUNT_TOKEN', '')
    if token:
        zap.replacer.add_rule(description='bearer token of the scan account', enabled='true', matchtype='REQ_HEADER',
                              matchstring='Authorization', matchregex='false', replacement='Bearer ' + token, initiators='')
