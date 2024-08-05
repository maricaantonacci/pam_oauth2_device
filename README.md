# PAM module for OAuth 2.0 Device flow

This is a PAM module based on a fork of [stfc/pam_oauth2_device](https://github.com/stfc/pam_oauth2_device) that lets you login using OpenID Connect credentials. It is tailored to work with the [INDIGO-IAM](https://indigo-iam.github.io/v/current/docs/) service. 

It uses the OAuth2 Device Flow, which means that during the login process, you will click a link and log in to your OpenID Connect Provider, which will then authenticate you. 

This module will then check if you're in the right group(s), if any has been specified in the module configuration, and allow or deny access.


## Installation (Ubuntu 22.04)

Download the debian package from the [releases page](https://github.com/maricaantonacci/pam_oauth2_device/releases) and install it, e.g.:

```
wget https://github.com/maricaantonacci/pam_oauth2_device/releases/download/v0.0.3/pam-oauth2-device_0.0.3_all.deb
sudo dpkg -i pam-oauth2-device_0.0.3_all.deb
```
The module and its configuration file will be installed:

```
/etc/pam_oauth2_device/config.json
/usr/lib/x86_64-linux-gnu/security/pam_oauth2_device.so
```

:warning: You have to edit the configuration file to make this module work.

## Build (Ubuntu 22.04)

**Requirements**: make, g++ (`sudo apt install build-essential`)

1. Download the code, e.g.
   ```
   git clone https://github.com/maricaantonacci/pam_oauth2_device
   ```

1. Install dependencies
   ``` 
   sudo apt install libcurl4-nss-dev libpam0g-dev
   ```

2. Compile
   ```
   cd pam_oauth2_device/
   make
   ```

## Configuration

The configuration should be located at `/etc/pam_oauth2_device/config.json`.

---
**NOTE** In order to proceed with the configuration, you need to register a client in IAM with the following characteristics:
   - scopes: `openid profile`
   - `device` grant
   - redirect uri is not used (you can set it to `https://<server ip>`)
   - The device code timeout default is 0. It should be a different value: set it to 300.
---

**oauth** - required section for your OAuth2 client configuration. 
  * client_id
  * client_secret
  * scope (default: openid profile)
  * device_endpoint
  * token_endpoint
  * userinfo_endpoint
  * username_attribute - specify the token claim to be used as username (p.e. preferred_username)
  * groups (default: []) - if a list of groups is provided, on login the users IAM groups will be checked against the group(s) specified. If they are in this group, they will be allowed in. An empty list (default) allows to disable the check.

**qr** - allowed correction levels are

  * -1 - no QR printed
  * 0 - low
  * 1 - medium
  * 2 - high

If no **qr** information is provided, the QR will not be printed.

**enable_email** - if set to true, the module will send by email the code to present to the user along with the url for the user to authenticate. In this case,  The section **send_mail** must specify the information needed to send the emails:

  * smtp_server_url
  * smtp_insecure
  * smtp_ca_path
  * smtp_username
  * smtp_password
  * mail_from
  * mail_from_username
  * mail_cc

**debug** - boolean value; if set to true, additional debugging information is printed to stdout by the module.

## Testing the module works

You are advised to do this before making changes to your SSH config.

Install `pamtester`:

```
sudo apt install pamtester
``` 

Edit `/etc/pam.d/pamtester`

```
auth required pam_oauth2_device.so
```
 
Run
```
pamtester -v pamtester user authenticate
```
and follow the onscreen prompts.

⚠️ __**user**__ is the name of the user account or, if you are enabling the email notification, is a valid user email address.  

You can check `/var/log/auth.log` and `/var/log/syslog` to find what's wrong if there are errors authenticating.
 
## Integrations

### SSH Configuration

Make sure the module works correctly before changing your SSH config or you may be locked out!

Edit `/etc/pam.d/sshd` and comment out the other `auth` sections.

```
auth required pam_oauth2_device.so /etc/pam_oauth2_device/config.json
```

Edit `/etc/ssh/sshd_config` and make sure that the following configuration options are set

```
ChallengeResponseAuthentication yes
UsePAM yes
```

```
systemctl restart sshd
```
