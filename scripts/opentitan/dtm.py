#!/usr/bin/env python3

# Copyright (c) 2024, Rivos, Inc.
# SPDX-License-Identifier: Apache2

"""Debug Transport Module tiny demo.

   :author: Emmanuel Blot <eblot@rivosinc.com>
"""

from argparse import ArgumentParser, Namespace, FileType
from io import BytesIO
from os import linesep
from os.path import dirname, normpath
from socket import create_connection
from traceback import format_exc
from typing import Optional
import sys

# pylint: disable=wrong-import-position
# pylint: disable=wrong-import-order
# pylint: disable=import-error

# JTAG module is available from the scripts/ directory
sys.path.append(normpath(dirname(dirname(sys.argv[0]))))

from ot.util.elf import ElfBlob  # noqa: E402
from ot.util.log import configure_loggers  # noqa: E402
from ot.util.misc import HexInt, dump_buffer  # noqa: E402
from ot.dtm import DebugTransportModule  # noqa: E402
from ot.dm import DebugModule  # noqa: E402
from jtag.bits import BitSequence  # noqa: E402
from jtag.bitbang import JtagBitbangController  # noqa: E402
from jtag.jtag import JtagEngine  # noqa: E402


DEFAULT_IR_LENGTH = 5
"""Default TAP Instruction Register length."""

DEFAULT_DMI_ADDRESS = 0x0
"""Default DMI address of the DM."""


def idcode(engine: JtagEngine, ir_length: int) -> None:
    """Retrieve ID code."""
    code = JtagBitbangController.INSTRUCTIONS['idcode']
    engine.write_ir(BitSequence(code, ir_length))
    value = engine.read_dr(32)
    engine.go_idle()
    return int(value)


def main():
    """Entry point."""
    debug = True
    try:
        args: Optional[Namespace] = None
        argparser = ArgumentParser(
            description=sys.modules[__name__].__doc__.split('.')[0])
        qvm = argparser.add_argument_group(title='Virtual machine')

        qvm.add_argument('-H', '--host', default='127.0.0.1',
                         help='JTAG host (default: localhost)')
        qvm.add_argument('-P', '--port', type=int,
                         default=JtagBitbangController.DEFAULT_PORT,
                         help=f'JTAG port, '
                              f'default: {JtagBitbangController.DEFAULT_PORT}')
        qvm.add_argument('-Q', '--no-quit', action='store_true', default=False,
                         help='do not ask the QEMU to quit on exit')
        dmi = argparser.add_argument_group(title='DMI')
        dmi.add_argument('-l', '--ir-length', type=int,
                         default=DEFAULT_IR_LENGTH,
                         help=f'bit length of the IR register '
                              f'(default: {DEFAULT_IR_LENGTH})')
        dmi.add_argument('-b', '--base', type=HexInt.parse,
                         default=DEFAULT_DMI_ADDRESS,
                         help=f'define DMI base address '
                              f'(default: 0x{DEFAULT_DMI_ADDRESS:x})')
        info = argparser.add_argument_group(title='Info')
        info.add_argument('-I', '--info', action='store_true',
                          help='report JTAG ID code and DTM configuration')
        info.add_argument('-c', '--csr',
                          help='read CSR value from hart')
        info.add_argument('-C', '--csr-check', type=HexInt.parse, default=None,
                          help='check CSR value matches')
        act = argparser.add_argument_group(title='Actions')
        act.add_argument('-x', '--execute', action='store_true',
                         help='update the PC from a loaded ELF file')
        act.add_argument('-X', '--no-exec', action='store_true', default=False,
                         help='does not resume hart execution')
        mem = argparser.add_argument_group(title='Memory')
        mem.add_argument('-a', '--address', type=HexInt.parse,
                         help='address of the first byte to access')
        mem.add_argument('-m', '--mem', choices=('read', 'write'),
                         help='access memory using System Bus')
        mem.add_argument('-s', '--size', type=HexInt.parse,
                         help='size in bytes of memory to access')
        mem.add_argument('-f', '--file',
                         help='file to read/write data for memory access')
        mem.add_argument('-e', '--elf', type=FileType('rb'),
                         help='load ELF file into memory')
        mem.add_argument('-F', '--fast-mode', default=False,
                         action='store_true',
                         help='do not check system bus status while '
                              'transfering')
        extra = argparser.add_argument_group(title='Extras')
        extra.add_argument('-v', '--verbose', action='count',
                           help='increase verbosity')
        extra.add_argument('-d', '--debug', action='store_true',
                           help='enable debug mode')

        args = argparser.parse_args()
        debug = args.debug

        configure_loggers(args.verbose, 'dtm.rvdm', -1, 'dtm', 'jtag')

        sock = create_connection((args.host, args.port), timeout=0.5)
        sock.settimeout(0.1)
        ctrl = JtagBitbangController(sock)
        eng = JtagEngine(ctrl)
        ctrl.tap_reset(True)
        ir_length = args.ir_length
        dtm = DebugTransportModule(eng, ir_length)
        rvdm = None
        try:
            if args.info:
                code = idcode(eng, ir_length)
                print(f'IDCODE:    0x{code:x}')
                version = dtm['dtmcs'].dmi_version
                abits = dtm['dtmcs'].abits
                print(f'DTM:       v{version[0]}.{version[1]}, {abits} bits')
                dtm['dtmcs'].check()
            dtm['dtmcs'].dmireset()
            if args.csr_check is not None and not args.csr:
                argparser.error('CSR check requires CSR option')
            if args.csr:
                if not rvdm:
                    rvdm = DebugModule(dtm, args.base)
                    rvdm.initialize()
                rvdm.halt()
                dmver = rvdm.status['version']
                if args.info:
                    print(f'DM:        {dmver.name}')
                sbver = rvdm.system_bus_info['sbversion']
                if args.info:
                    print(f'SYSBUS:    {sbver.name}')
                if not args.csr:
                    csr = 'misa'
                else:
                    try:
                        csr = HexInt.parse(args.csr)
                    except ValueError:
                        csr = args.csr
                csr_val = rvdm.read_csr(csr)
                if not args.no_exec:
                    rvdm.resume()
                if args.csr_check is not None:
                    if csr_val != args.csr_check:
                        raise RuntimeError(f'CSR {args.csr} check failed: '
                                           f'0x{csr_val:08x} != '
                                           f'0x{args.csr_check:08x}')
                else:
                    pad = ' ' * (10 - len(args.csr))
                    print(f'{args.csr}:{pad}0x{csr_val:08x}')
            if args.mem:
                if args.address is None:
                    argparser.error('no address specified for memory operation')
                if args.mem == 'write' and not args.file:
                    argparser.error('no file specified for mem write operation')
                if args.mem == 'read' and not args.size:
                    argparser.error('no size specified for mem read operation')
                if not rvdm:
                    rvdm = DebugModule(dtm, args.base)
                    rvdm.initialize()
                try:
                    rvdm.halt()
                    if args.file:
                        mode = 'rb' if args.mem == 'write' else 'wb'
                        with open(args.file, mode) as mfp:
                            rvdm.memory_copy(mfp, args.mem, args.address,
                                             args.size, no_check=args.fast_mode)
                    else:
                        mfp = BytesIO()
                        rvdm.memory_copy(mfp, args.mem, args.address, args.size,
                                         no_check=args.fast_mode)
                        dump_buffer(mfp, args.address)
                finally:
                    if not args.no_exec:
                        rvdm.resume()
            if args.elf:
                if not ElfBlob.LOADED:
                    argparser.error('pyelftools module not available')
                elf = ElfBlob()
                elf.load(args.elf)
                args.elf.close()
                if elf.address_size != 32:
                    argparser.error('Only ELF32 files are supported')
                if not rvdm:
                    rvdm = DebugModule(dtm, args.base)
                    rvdm.initialize()
                try:
                    rvdm.halt()
                    mfp = BytesIO(elf.blob)
                    rvdm.memory_copy(mfp, 'write', elf.load_address, args.size,
                                     no_check=args.fast_mode)
                    if args.execute:
                        rvdm.set_pc(elf.entry_point)
                finally:
                    if args.execute or not args.no_exec:
                        rvdm.resume()
            else:
                if args.execute:
                    argparser.error('Cannot execute without loaded an ELF file')
        finally:
            if not args.no_quit:
                ctrl.quit()

    # pylint: disable=broad-except
    except Exception as exc:
        print(f'{linesep}Error: {exc}', file=sys.stderr)
        if debug:
            print(format_exc(chain=False), file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(2)


if __name__ == '__main__':
    main()
