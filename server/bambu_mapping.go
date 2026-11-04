package main

import "fmt"

type bambuTrayAddress struct {
	AMS  int `json:"ams_id"`
	Slot int `json:"slot_id"`
}

func isExternalTray(id int) bool { return id == 254 || id == 255 }

func validBambuTray(id int) bool {
	return id >= 0 && id < 64 || id >= 128 && id < 136 || isExternalTray(id)
}

func bambuMapping(mapping []int, addresses []bambuTrayAddress, count int) ([]int, []bambuTrayAddress, error) {
	if len(mapping) != count || addresses != nil && len(addresses) != count {
		return nil, nil, fmt.Errorf("AMS mapping needs one tray per job filament")
	}
	v0 := make([]int, count)
	v1 := make([]bambuTrayAddress, count)
	for i, id := range mapping {
		if !validBambuTray(id) {
			return nil, nil, fmt.Errorf("AMS mapping has an invalid tray id %d", id)
		}
		v0[i] = id
		switch {
		case isExternalTray(id):
			v0[i], v1[i] = -1, bambuTrayAddress{id, 0}
		case id >= 128:
			v1[i] = bambuTrayAddress{id, 0}
		default:
			v1[i] = bambuTrayAddress{id / 4, id % 4}
		}
		if addresses != nil {
			a := addresses[i]
			matches := a.AMS*4+a.Slot == id
			if id >= 128 {
				matches = a.AMS == id && a.Slot == 0
			}
			if id >= 24 && id <= 27 && a.AMS < 16 && a.Slot == id-24 {
				matches = true
			}
			if a.AMS < 0 || a.AMS > 255 || a.Slot < 0 || a.Slot > 3 || !matches {
				return nil, nil, fmt.Errorf("AMS mapping has an invalid unit or slot at filament %d", i+1)
			}
			v1[i] = a
		}
	}
	return v0, v1, nil
}
