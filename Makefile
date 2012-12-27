all: isv.pdf

isv.pdf: isv.tex books/*.tex
	pdflatex isv
	pdflatex isv
	pdflatex isv
