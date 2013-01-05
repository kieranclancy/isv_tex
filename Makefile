COMMON:= \
	size-ebook.tex \
	isv-common.tex \
	newtestament-books.tex \
	oldtestament-books \
	redletter.tex \
	blackletter.tex \
	books/*.tex

all: newtestament.pdf newtestament-black.pdf oldtestament.pdf bible.pdf

%.pdf: %.tex
	pdflatex $<
	pdflatex $<
	pdflatex $<
