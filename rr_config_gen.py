#!/usr/bin/env python3
"""
RadioReference SOAP API → trunk-recorder config generator.

Pulls system details, site frequencies, and talkgroups from the
RadioReference SOAP API and generates the config.json + talkgroups.csv
files needed by the P25 control channel logger.

Requirements:
  pip install zeep

Usage:
  python rr_config_gen.py \
    --system-id 10471 \
    --api-key YOUR_RR_API_KEY \
    --username YOUR_RR_USERNAME \
    --password YOUR_RR_PASSWORD \
    --sdr-device "rtl=0" \
    --sdr-rate 2048000 \
    --sdr-gain 38 \
    --output-dir ./p25-logger
"""

import argparse
import json
import csv
import sys
import os
from pathlib import Path

try:
    from zeep import Client
    from zeep.exceptions import Fault
except ImportError:
    print("ERROR: 'zeep' package required. Install with: pip install zeep")
    sys.exit(1)

WSDL_URL = "https://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc"


def get_auth_info(args):
    """Build the authInfo dict required by all RR SOAP methods."""
    return {
        "appKey": args.api_key,
        "username": args.username,
        "password": args.password,
        "version": "latest",
        "style": "rpc",
    }


def fetch_system_details(client, system_id, auth_info):
    """Call getTrsDetails to get system overview (type, WACN, sysid, etc.)."""
    try:
        result = client.service.getTrsDetails(system_id, auth_info)
        return result
    except Fault as e:
        print(f"ERROR fetching system details: {e}")
        sys.exit(1)


def fetch_sites(client, system_id, auth_info):
    """Call getTrsSites to get all sites with their frequencies."""
    try:
        result = client.service.getTrsSites(system_id, auth_info)
        return result
    except Fault as e:
        print(f"ERROR fetching sites: {e}")
        sys.exit(1)


def fetch_talkgroups(client, system_id, auth_info):
    """Call getTrsTalkgroups to get all talkgroup categories and talkgroups."""
    try:
        result = client.service.getTrsTalkgroups(system_id, auth_info)
        return result
    except Fault as e:
        print(f"ERROR fetching talkgroups: {e}")
        sys.exit(1)


def extract_control_channels(sites):
    """Extract control channel frequencies from site data."""
    control_channels = []

    for site in sites:
        if not hasattr(site, "siteFreqs") or site.siteFreqs is None:
            continue

        for freq_obj in site.siteFreqs:
            freq = float(freq_obj.freq) * 1e6  # Convert MHz to Hz
            # Check if this is a control channel (use attribute if available)
            is_cc = False
            if hasattr(freq_obj, "use"):
                use = str(freq_obj.use).lower() if freq_obj.use else ""
                is_cc = "c" in use or "control" in use
            if hasattr(freq_obj, "flags"):
                flags = str(freq_obj.flags).lower() if freq_obj.flags else ""
                is_cc = is_cc or "c" in flags

            if is_cc and freq not in control_channels:
                control_channels.append(int(freq))

    # If we couldn't identify control channels specifically, return all freqs
    if not control_channels:
        print("WARNING: Could not identify specific control channels from site data.")
        print("         Including all site frequencies. You may need to edit config.json.")
        for site in sites:
            if not hasattr(site, "siteFreqs") or site.siteFreqs is None:
                continue
            for freq_obj in site.siteFreqs:
                freq = int(float(freq_obj.freq) * 1e6)
                if freq not in control_channels:
                    control_channels.append(freq)

    return sorted(control_channels)


def calculate_center_frequency(control_channels, sample_rate):
    """Calculate optimal SDR center frequency to cover all control channels."""
    if not control_channels:
        return 0

    min_freq = min(control_channels)
    max_freq = max(control_channels)
    center = (min_freq + max_freq) // 2
    span = max_freq - min_freq

    # Verify sample rate covers the span (with margin)
    if span > sample_rate * 0.8:
        print(f"WARNING: Control channels span {span/1e6:.3f} MHz but sample rate")
        print(f"         only covers {sample_rate/1e6:.3f} MHz. Increase --sdr-rate.")

    return center


def flatten_talkgroups(tg_categories):
    """Recursively extract talkgroups from nested category structure."""
    talkgroups = []

    if tg_categories is None:
        return talkgroups

    for cat in tg_categories:
        category_name = cat.tgCatName if hasattr(cat, "tgCatName") else "Unknown"

        # Get talkgroups in this category
        if hasattr(cat, "tgs") and cat.tgs is not None:
            for tg in cat.tgs:
                tg_id = int(tg.tgDec) if hasattr(tg, "tgDec") else 0
                tg_hex = format(tg_id, "04X")
                alpha_tag = str(tg.tgAlpha) if hasattr(tg, "tgAlpha") else ""
                description = str(tg.tgDescr) if hasattr(tg, "tgDescr") else ""
                tag = str(tg.tgTag) if hasattr(tg, "tgTag") else ""

                # Determine mode
                mode = "D"  # Default digital
                if hasattr(tg, "tgMode"):
                    mode_str = str(tg.tgMode).upper() if tg.tgMode else "D"
                    if mode_str in ("A", "ANALOG"):
                        mode = "A"
                    elif mode_str in ("E", "ENCRYPTED"):
                        mode = "E"
                    elif mode_str in ("DE", "D/E"):
                        mode = "DE"
                    elif mode_str in ("TE"):
                        mode = "TE"

                # Check encryption
                enc = 0
                if hasattr(tg, "enc"):
                    enc = int(tg.enc) if tg.enc else 0
                if enc == 2:
                    mode = "E"
                elif enc == 1 and mode == "D":
                    mode = "DE"

                talkgroups.append({
                    "decimal": tg_id,
                    "hex": tg_hex,
                    "alpha_tag": alpha_tag.strip(),
                    "mode": mode,
                    "description": description.strip(),
                    "tag": tag.strip(),
                    "category": category_name.strip(),
                })

        # Recurse into subcategories
        if hasattr(cat, "subCats") and cat.subCats is not None:
            talkgroups.extend(flatten_talkgroups(cat.subCats))

    return talkgroups


def generate_config(control_channels, args, system_details=None):
    """Generate config.json for the P25 logger."""
    center = calculate_center_frequency(control_channels, args.sdr_rate)

    # Determine modulation from system details if available
    modulation = "qpsk"  # Default for P25 Phase I

    config = {
        "ver": 2,
        "logLevel": "info",
        "consoleLog": True,
        "logFile": False,
        "callTimeout": 3,
        "frequencyFormat": "mhz",
        "controlWarnRate": 10,
        "systems": [
            {
                "shortName": args.short_name,
                "type": "p25",
                "control_channels": control_channels,
                "modulation": modulation,
                "talkgroupsFile": "talkgroups.csv",
                "recordUnknown": True,
                "hideUnknownTalkgroups": False,
                "hideEncrypted": False,
                "digitalLevels": 1,
                "squelch": -160,
                "enabled": True,
            }
        ],
        "sources": [
            {
                "driver": "osmosdr",
                "device": args.sdr_device,
                "center": center,
                "rate": args.sdr_rate,
                "gain": args.sdr_gain,
                "digitalRecorders": 0,
                "analogRecorders": 0,
                "enabled": True,
            }
        ],
    }

    return config


def generate_talkgroups_csv(talkgroups):
    """Generate talkgroups.csv content."""
    rows = []
    for tg in sorted(talkgroups, key=lambda t: t["decimal"]):
        rows.append([
            tg["decimal"],
            tg["hex"],
            tg["alpha_tag"],
            tg["mode"],
            tg["description"],
            tg["tag"],
            tg["category"],
        ])
    return rows


def main():
    parser = argparse.ArgumentParser(
        description="Generate trunk-recorder config from RadioReference API"
    )
    parser.add_argument("--system-id", type=int, required=True,
                        help="RadioReference trunked system ID (e.g., 10471)")
    parser.add_argument("--api-key", required=True,
                        help="RadioReference API application key")
    parser.add_argument("--username", required=True,
                        help="RadioReference Premium username")
    parser.add_argument("--password", required=True,
                        help="RadioReference Premium password")
    parser.add_argument("--short-name", default="p25sys",
                        help="Short name for the system (default: p25sys)")
    parser.add_argument("--sdr-device", default="rtl=0",
                        help="SDR device string (default: rtl=0)")
    parser.add_argument("--sdr-rate", type=int, default=2048000,
                        help="SDR sample rate in Hz (default: 2048000)")
    parser.add_argument("--sdr-gain", type=float, default=38,
                        help="SDR gain (default: 38)")
    parser.add_argument("--output-dir", default=".",
                        help="Output directory for config files (default: .)")

    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Connecting to RadioReference SOAP API...")
    client = Client(WSDL_URL)
    auth_info = get_auth_info(args)

    # Fetch system details
    print(f"Fetching system details for ID {args.system_id}...")
    system_details = fetch_system_details(client, args.system_id, auth_info)
    sys_type = system_details.sType if hasattr(system_details, "sType") else "Unknown"
    sys_name = system_details.sName if hasattr(system_details, "sName") else "Unknown"
    print(f"  System: {sys_name}")
    print(f"  Type: {sys_type}")

    if hasattr(system_details, "wacn") and system_details.wacn:
        print(f"  WACN: {system_details.wacn}")
    if hasattr(system_details, "sysid") and system_details.sysid:
        print(f"  System ID: {system_details.sysid}")

    # Fetch sites
    print(f"Fetching sites...")
    sites = fetch_sites(client, args.system_id, auth_info)
    if sites:
        print(f"  Found {len(sites)} site(s)")
        for site in sites:
            site_name = site.siteDescr if hasattr(site, "siteDescr") else "Unknown"
            site_num = site.siteNumber if hasattr(site, "siteNumber") else "?"
            nac = site.siteNac if hasattr(site, "siteNac") else "?"
            rfss = site.siteRfss if hasattr(site, "siteRfss") else "?"
            print(f"    Site {site_num}: {site_name} (NAC: {nac}, RFSS: {rfss})")
    else:
        print("  ERROR: No sites found!")
        sys.exit(1)

    # Extract control channels
    control_channels = extract_control_channels(sites)
    print(f"  Control channels: {[f/1e6 for f in control_channels]} MHz")

    # Fetch talkgroups
    print(f"Fetching talkgroups...")
    tg_categories = fetch_talkgroups(client, args.system_id, auth_info)
    talkgroups = flatten_talkgroups(tg_categories)
    print(f"  Found {len(talkgroups)} talkgroup(s)")

    # Generate config.json
    config = generate_config(control_channels, args, system_details)
    config_path = output_dir / "config.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)
    print(f"\nWrote {config_path}")

    # Generate talkgroups.csv
    tg_path = output_dir / "talkgroups.csv"
    tg_rows = generate_talkgroups_csv(talkgroups)
    with open(tg_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Decimal", "Hex", "Alpha Tag", "Mode", "Description", "Tag", "Category"])
        writer.writerows(tg_rows)
    print(f"Wrote {tg_path} ({len(tg_rows)} talkgroups)")

    # Summary
    print(f"\nDone! Files written to {output_dir}/")
    print(f"Center frequency: {config['sources'][0]['center']/1e6:.4f} MHz")
    print(f"Control channels: {len(control_channels)}")
    print(f"Talkgroups: {len(tg_rows)}")


if __name__ == "__main__":
    main()
