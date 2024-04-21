{ lib
, python3Packages
, fetchPypi
, imagemagick
}:

python3Packages.buildPythonApplication rec {
  pname = "iiif-validator";
  version = "1.0.5";
  format = "setuptools";

  src = fetchPypi {
    inherit pname version;
    sha256 = "yKX67pVhFmgnho7RISUl/JiZXS75k972AYVPOxqxyMM=";
  };

  propagatedBuildInputs = [
    python3Packages.bottle
    python3Packages.lxml
    python3Packages.pillow
    python3Packages.python-magic
    imagemagick
  ];

  doCheck = false;

  nativeCheckInputs = [
    python3Packages.mock
  ];

  meta = with lib; {
    description = "This validator supports the same validations that are available on the IIIF website at http://iiif.io/api/image/validator/";
    homepage = "https://github.com/IIIF/image-validator";
    license = licenses.asl20;
    maintainers = [ maintainers.subotic ];
  };
}
