/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include "../device_parsing.h"
#include "../device_parsing.c"

const char *xml =
"<domain type='xen' id='2'>"
"  <name>pv1</name>"
"  <uuid>a3be4e7c539a3fb734648b11819bd9c3</uuid>"
"  <bootloader>/usr/lib/xen/boot/domUloader.py</bootloader>"
"  <bootloader_args>--entry=xvda2:/boot/vmlinuz-xen,/boot/initrd-xen</bootloader_args>"
"  <os>"
"    <type>linux</type>"
"    <kernel>/var/lib/xen/tmp/kernel.5ihM-L</kernel>"
"    <initrd>/var/lib/xen/tmp/ramdisk.G2vsaq</initrd>"
"    <cmdline>TERM=xterm </cmdline>"
"  </os>"
"  <memory>524288</memory>"
"  <vcpu>1</vcpu>"
"  <on_poweroff>destroy</on_poweroff>"
"  <on_reboot>restart</on_reboot>"
"  <on_crash>destroy</on_crash>"
"  <devices>"
"    <interface type='bridge'>"
"      <mac address='00:16:3e:3a:83:91'/>"
"      <script path='vif-bridge'/>"
"    </interface>"
"    <disk type='file' device='disk'>"
"      <driver name='file'/>"
"      <source file='/var/lib/xen/images/pv1/disk0'/>"
"      <target dev='xvda'/>"
"    </disk>"
"    <input type='mouse' bus='xen'/>"
"    <graphics type='vnc' port='5900'/>"
"    <console tty='/dev/pts/3'/>"
"  </devices>"
	"</domain>";


int main(int argc, char **argv)
{
	struct domain dom;
	struct domain *domptr = &dom;

	_get_dominfo(xml, &dom);

	printf("Name: %s\n"
	       "UUID: %s\n"
	       "Bootloader: %s\n",
	       dom.name, dom.uuid, dom.bootloader);

	//cleanup_dominfo(&

	return 0;
}
