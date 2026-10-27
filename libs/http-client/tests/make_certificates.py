#!/usr/bin/env python3
"""Disposable fixtures; cryptography is only a development dependency."""
import datetime as dt
import pathlib
import sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
now=dt.datetime.now(dt.timezone.utc)
def name(text):return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,text)])
def key(kind='p256'):
    if kind=='rsa':return rsa.generate_private_key(public_exponent=65537,key_size=2048)
    return ec.generate_private_key(ec.SECP384R1() if kind=='p384' else ec.SECP256R1())
def certificate(subject,private,issuer,issuer_key,ca=False,host='localhost',before=-1,after=7,algorithm=None):
    builder=(x509.CertificateBuilder().subject_name(subject).issuer_name(issuer)
        .public_key(private.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now+dt.timedelta(days=before)).not_valid_after(now+dt.timedelta(days=after))
        .add_extension(x509.BasicConstraints(ca=ca,path_length=1 if ca else None),critical=True))
    if ca:
        builder=builder.add_extension(x509.KeyUsage(False,False,False,False,False,True,True,False,False),critical=True)
    else:
        builder=(builder.add_extension(x509.SubjectAlternativeName([x509.DNSName(host)]),critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),critical=False))
    return builder.sign(issuer_key,algorithm or hashes.SHA256())
def pem(cert):return cert.public_bytes(serialization.Encoding.PEM)
def save(label,cert,private,ca,chain=b''):
    (out/(label+'.pem')).write_bytes(pem(cert)+chain)
    (out/(label+'.ca.pem')).write_bytes(pem(ca))
    (out/(label+'.key')).write_bytes(private.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))

root_key=key();root_name=name('InfoHub prototype root')
root=certificate(root_name,root_key,root_name,root_key,ca=True,after=30)
(out/'root.pem').write_bytes(pem(root))
for label,host,before,after,self_signed in [
    ('valid','localhost',-1,7,False),('expired','localhost',-7,-1,False),
    ('future','localhost',1,7,False),('wrong-host','other.invalid',-1,7,False),
    ('untrusted','localhost',-1,7,True),('valid-tls12','localhost',-1,7,False),
]:
    private=key();subject=name(host)
    cert=certificate(subject,private,subject if self_signed else root_name,private if self_signed else root_key,host=host,before=before,after=after)
    save(label,cert,private,root)

for label,kind,digest,host,before,after in [
    ('valid-rsa','rsa',hashes.SHA256(),'localhost',-1,7),
    ('valid-rsa-sha512','rsa',hashes.SHA512(),'localhost',-1,7),
    ('valid-p384','p384',hashes.SHA384(),'localhost',-1,7),
    ('expired-rsa','rsa',hashes.SHA256(),'localhost',-7,-1),
    ('wrong-host-rsa','rsa',hashes.SHA256(),'other.invalid',-1,7),
]:
    ca_key=key(kind);ca_name=name('InfoHub '+label+' root')
    ca=certificate(ca_name,ca_key,ca_name,ca_key,ca=True,after=30,algorithm=digest)
    private=key('rsa' if kind=='rsa' else 'p256')
    cert=certificate(name(host),private,ca_name,ca_key,host=host,before=before,after=after,algorithm=digest)
    save(label,cert,private,ca)

intermediate_key=key();intermediate_name=name('InfoHub intermediate')
intermediate=certificate(intermediate_name,intermediate_key,root_name,root_key,ca=True,after=14)
private=key();leaf=certificate(name('localhost'),private,intermediate_name,intermediate_key)
save('valid-intermediate',leaf,private,root,pem(intermediate))
print('Generated disposable TLS fixtures:',out)
