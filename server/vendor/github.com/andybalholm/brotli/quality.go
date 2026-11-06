package brotli

const fastOnePassCompressionQuality = 0

const fastTwoPassCompressionQuality = 1

const zopflificationQuality = 10

const hqZopflificationQuality = 11

const maxQualityForStaticEntropyCodes = 2

const minQualityForBlockSplit = 4

const minQualityForNonzeroDistanceParams = 4

const minQualityForOptimizeHistograms = 4

const minQualityForExtensiveReferenceSearch = 5

const minQualityForContextModeling = 5

const minQualityForHqContextModeling = 7

const minQualityForHqBlockSplitting = 10

const maxNumDelayedSymbols = 0x2FFF

func maxHashTableSize(quality int) uint {
	if quality == fastOnePassCompressionQuality {
		return 1 << 15
	} else {
		return 1 << 17
	}
}

const maxZopfliLenQuality10 = 150

const maxZopfliLenQuality11 = 325

const longCopyQuickStep = 16384

func maxZopfliLen(params *encoderParams) uint {
	if params.quality <= 10 {
		return maxZopfliLenQuality10
	} else {
		return maxZopfliLenQuality11
	}
}

func maxZopfliCandidates(params *encoderParams) uint {
	if params.quality <= 10 {
		return 1
	} else {
		return 5
	}
}

func sanitizeParams(params *encoderParams) {
	params.quality = brotli_min_int(maxQuality, brotli_max_int(minQuality, params.quality))
	if params.quality <= maxQualityForStaticEntropyCodes {
		params.large_window = false
	}

	if params.lgwin < minWindowBits {
		params.lgwin = minWindowBits
	} else {
		var max_lgwin int
		if params.large_window {
			max_lgwin = largeMaxWindowBits
		} else {
			max_lgwin = maxWindowBits
		}
		if params.lgwin > uint(max_lgwin) {
			params.lgwin = uint(max_lgwin)
		}
	}
}

func computeLgBlock(params *encoderParams) int {
	var lgblock int = params.lgblock
	if params.quality == fastOnePassCompressionQuality || params.quality == fastTwoPassCompressionQuality {
		lgblock = int(params.lgwin)
	} else if params.quality < minQualityForBlockSplit {
		lgblock = 14
	} else if lgblock == 0 {
		lgblock = 16
		if params.quality >= 9 && params.lgwin > uint(lgblock) {
			lgblock = brotli_min_int(18, int(params.lgwin))
		}
	} else {
		lgblock = brotli_min_int(maxInputBlockBits, brotli_max_int(minInputBlockBits, lgblock))
	}

	return lgblock
}

func computeRbBits(params *encoderParams) int {
	return 1 + brotli_max_int(int(params.lgwin), params.lgblock)
}

func maxMetablockSize(params *encoderParams) uint {
	var bits int = brotli_min_int(computeRbBits(params), maxInputBlockBits)
	return uint(1) << uint(bits)
}

func literalSpreeLengthForSparseSearch(params *encoderParams) uint {
	if params.quality < 9 {
		return 64
	} else {
		return 512
	}
}

func chooseHasher(params *encoderParams, hparams *hasherParams) {
	if params.quality > 9 {
		hparams.type_ = 10
	} else if params.quality == 4 && params.size_hint >= 1<<20 {
		hparams.type_ = 54
	} else if params.quality < 5 {
		hparams.type_ = params.quality
	} else if params.lgwin <= 16 {
		if params.quality < 7 {
			hparams.type_ = 40
		} else if params.quality < 9 {
			hparams.type_ = 41
		} else {
			hparams.type_ = 42
		}
	} else if params.size_hint >= 1<<20 && params.lgwin >= 19 {
		hparams.type_ = 6
		hparams.block_bits = params.quality - 1
		hparams.bucket_bits = 15
		hparams.hash_len = 5
		if params.quality < 7 {
			hparams.num_last_distances_to_check = 4
		} else if params.quality < 9 {
			hparams.num_last_distances_to_check = 10
		} else {
			hparams.num_last_distances_to_check = 16
		}
	} else {
		hparams.type_ = 5
		hparams.block_bits = params.quality - 1
		if params.quality < 7 {
			hparams.bucket_bits = 14
		} else {
			hparams.bucket_bits = 15
		}
		if params.quality < 7 {
			hparams.num_last_distances_to_check = 4
		} else if params.quality < 9 {
			hparams.num_last_distances_to_check = 10
		} else {
			hparams.num_last_distances_to_check = 16
		}
	}

	if params.lgwin > 24 {

		if hparams.type_ == 3 {
			hparams.type_ = 35
		}

		if hparams.type_ == 54 {
			hparams.type_ = 55
		}

		if hparams.type_ == 6 {
			hparams.type_ = 65
		}
	}
}
